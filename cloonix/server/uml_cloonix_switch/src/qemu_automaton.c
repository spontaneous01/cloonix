/*****************************************************************************/
/*    Copyright (C) 2006-2017 cloonix@cloonix.net License AGPL-3             */
/*                                                                           */
/*  This program is free software: you can redistribute it and/or modify     */
/*  it under the terms of the GNU Affero General Public License as           */
/*  published by the Free Software Foundation, either version 3 of the       */
/*  License, or (at your option) any later version.                          */
/*                                                                           */
/*  This program is distributed in the hope that it will be useful,          */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of           */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            */
/*  GNU Affero General Public License for more details.a                     */
/*                                                                           */
/*  You should have received a copy of the GNU Affero General Public License */
/*  along with this program.  If not, see <http://www.gnu.org/licenses/>.    */
/*                                                                           */
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>


#include "io_clownix.h"
#include "rpc_clownix.h"
#include "cfg_store.h"
#include "commun_daemon.h"
#include "event_subscriber.h"
#include "pid_clone.h"
#include "machine_create.h"
#include "heartbeat.h"
#include "system_callers.h"
#include "automates.h"
#include "util_sock.h"
#include "utils_cmd_line_maker.h"
#include "qmonitor.h"
#include "qmp.h"
#include "qhvc0.h"
#include "doorways_mngt.h"
#include "doors_rpc.h"
#include "file_read_write.h"
#include "endp_mngt.h"

#define DRIVE_PARAMS_CISCO " -drive file=%s,index=%d,media=disk,if=virtio,cache=directsync"
#define DRIVE_PARAMS " -drive file=%s,index=%d,media=disk,if=virtio"

#define VIRTIO_9P " -fsdev local,id=fsdev0,security_model=passthrough,path=%s"\
                  " -device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=%s"

#define DRIVE_FULL_VIRT " -drive file=%s,index=%d,media=disk,if=ide"

#define INSTALL_DISK " -boot d -drive file=%s,index=%d,media=disk,if=virtio"

#define AGENT_CDROM " -drive file=%s,if=none,media=cdrom,id=cd"\
                    " -device virtio-scsi-pci"\
                    " -device scsi-disk,drive=cd"

#define AGENT_DISK " -drive file=%s,if=virtio,media=disk"


#define ADDED_CDROM " -drive file=%s,media=cdrom"

typedef struct t_cprootfs_config
{
  char name[MAX_NAME_LEN];
  char msg[MAX_PRINT_LEN];
  char backing[MAX_PATH_LEN];
  char used[MAX_PATH_LEN];
} t_cprootfs_config;


enum
  {
  auto_idle = 0,
  auto_create_disk,
  auto_create_vm_launch,
  auto_create_vm_connect,
  auto_max,
  };

/*--------------------------------------------------------------------------*/

int inside_cloonix(char **name);

void qemu_vm_automaton(void *unused_data, int status, char *name);

char **get_saved_environ(void);

/****************************************************************************/
static int get_wake_up_eths(char *name, t_vm **vm,
                            t_wake_up_eths **wake_up_eths)
{
  *vm = cfg_get_vm(name);
  if (!(*vm))
    return -1;
  *wake_up_eths = (*vm)->wake_up_eths;
  if (!(*wake_up_eths))
    return -1;
  return 0;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void static_vm_timeout(void *data)
{
  t_wake_up_eths *wake_up_eths = (t_wake_up_eths *) data;
  if (!wake_up_eths)
    KOUT(" ");
  qemu_vm_automaton(NULL, 0, wake_up_eths->name);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void cprootfs_clone_death(void *data, int status, char *name)
{
  t_cprootfs_config *cprootfs = (t_cprootfs_config *) data;
  t_vm   *vm;
  t_wake_up_eths *wake_up_eths;

  event_print("%s %s", __FUNCTION__, name);
  if (!get_wake_up_eths(name, &vm, &wake_up_eths))
    {
    if (strcmp(name, cprootfs->name))
      KOUT("%s %s", name, cprootfs->name);
    if (strstr(cprootfs->msg, "OK"))
      {
      if (status)
        KOUT("%d", status);
      }
    else if (strstr(cprootfs->msg, "KO"))
      {
      if (!status)
        KOUT("%d", status);
      snprintf(wake_up_eths->error_report, MAX_PRINT_LEN-1, 
               "%s", cprootfs->msg);
      KERR("%s", name);
      }
    else
      KERR("%s %d", cprootfs->msg, status);
    qemu_vm_automaton(NULL, status, name);
    }
  clownix_free(cprootfs, __FUNCTION__);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void cprootfs_clone_msg(void *data, char *msg)
{
  int pid;
  t_cprootfs_config *cprootfs = (t_cprootfs_config *) data;
  t_vm   *vm;
  t_wake_up_eths *wake_up_eths;
  if (!get_wake_up_eths(cprootfs->name, &vm, &wake_up_eths))
    {
    if (!strncmp(msg, "pid=", strlen("pid=")))
      {
      if (!strncmp(msg, "pid=start", strlen("pid=start")))
        {
        if (sscanf(msg, "pid=start:%d", &(vm->pid_of_cp_clone)) != 1)
          KOUT("%s", msg);
        }
      else if (!strncmp(msg, "pid=end", strlen("pid=end")))
        {
        if (sscanf(msg, "pid=end:%d", &pid) != 1)
          KOUT("%s", msg);
        if (pid != vm->pid_of_cp_clone)
          KERR(" %s %d", msg, vm->pid_of_cp_clone);
        vm->pid_of_cp_clone = 0;
        }
      else
        KERR(" %s", msg);
      }
    else
      strncpy(cprootfs->msg, msg, MAX_NAME_LEN-1);
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int local_clownix_system (char *commande)
{
  pid_t pid;
  int   status;
  char **environ = NULL;
  char * argv [4];
  char msg_dad[MAX_NAME_LEN];
  if (commande == NULL)
    return (1);
  if ((pid = fork ()) < 0)
    return (-1);
  if (pid == 0)
    {
    argv[0] = "/bin/bash";
    argv[1] = "-c";
    argv[2] = commande;
    argv[3] = NULL;
    execve("/bin/bash", argv, environ);
    exit (127);
    }
  memset(msg_dad, 0, MAX_NAME_LEN);
  snprintf(msg_dad, MAX_NAME_LEN - 1, "pid=start:%d", pid);
  send_to_daddy(msg_dad);
  while (1)
    {
    if (waitpid (pid, &status, 0) == -1)
      return (-1);
    else
      {
      memset(msg_dad, 0, MAX_NAME_LEN);
      snprintf(msg_dad, MAX_NAME_LEN - 1, "pid=end:%d", pid);
      send_to_daddy(msg_dad);
      return (status);
      }
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int cprootfs_clone(void *data)
{
  int result;
  char err[MAX_PRINT_LEN];
  char *cmd;
  t_cprootfs_config *cprootfs = (t_cprootfs_config *) data;
  memset(err, 0, MAX_PRINT_LEN);
  strcpy(err, "KO ");
  cmd = utils_qemu_img_derived(cprootfs->backing, cprootfs->used);
  result = local_clownix_system(cmd);
  if (result)
    KERR("%s", cmd);
  snprintf(cmd, 2*MAX_PATH_LEN, "/bin/chmod +w %s", cprootfs->used);
  result = clownix_system(cmd);
  if (result)
    KERR("%s", cmd);
  if (result)
    {
    send_to_daddy(err);
    KERR("%s", cmd);
    }
  else
    {
    send_to_daddy("OK");
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void derived_file_creation_request(t_vm *vm)
{
  char *name;
  t_cprootfs_config *cprootfs;
  if (!vm)
    KOUT(" ");
  name = vm->kvm.name;
  cprootfs=(t_cprootfs_config *)clownix_malloc(sizeof(t_cprootfs_config),13);
  memset(cprootfs, 0, sizeof(t_cprootfs_config));
  strncpy(cprootfs->name, name, MAX_NAME_LEN-1);
  strcpy(cprootfs->msg, "NO_MSG");

  strncpy(cprootfs->used, vm->kvm.rootfs_used, MAX_PATH_LEN-1);
  strncpy(cprootfs->backing, vm->kvm.rootfs_backing, MAX_PATH_LEN-1);

  event_print("%s %s", __FUNCTION__, name);
  pid_clone_launch(cprootfs_clone, cprootfs_clone_death,
                   cprootfs_clone_msg, cprootfs, 
                   cprootfs, cprootfs, name, -1, 1);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static char *format_virtkvm_net(t_vm *vm, int eth)
{
  static char net_cmd[MAX_PATH_LEN*3];
  int len = 0;
  char *mac_addr;
  len+=sprintf(net_cmd+len,
               " -device virtio-muethnet,tx=bh,netdev=eth%d,mac=", eth);
  mac_addr = vm->kvm.eth_params[eth].mac_addr;
  len += sprintf(net_cmd+len,"%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0] & 0xFF, mac_addr[1] & 0xFF, mac_addr[2] & 0xFF,
                 mac_addr[3] & 0xFF, mac_addr[4] & 0xFF, mac_addr[5] & 0xFF);
  len += sprintf(net_cmd+len, 
  " -netdev mueth,id=eth%d,munetname=%s,muname=%s,munum=%d,sock=%s,mutype=1",
                 eth,cfg_get_cloonix_name(), vm->kvm.name, eth, 
                 utils_get_endp_path(vm->kvm.name, eth)); 
  return net_cmd;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
#define QEMU_OPTS_BASE \
   " -m %d"\
   " -name %s"\
   " -serial stdio"\
   " -nodefaults"\
   " -rtc base=utc,driftfix=slew"\
   " -global kvm-pit.lost_tick_policy=delay"\
   " -no-hpet -no-shutdown -boot strict=on"\
   " -chardev socket,id=mon1,path=%s,server,nowait"\
   " -mon chardev=mon1,mode=readline"\
   " -chardev socket,id=qmp1,path=%s,server,nowait"\
   " -mon chardev=qmp1,mode=control"


#define QEMU_OPTS_CLOONIX \
   " -device virtio-serial-pci"\
   " -device virtio-mouse-pci"\
   " -device virtio-keyboard-pci"\
   " -chardev socket,path=%s,server,nowait,id=cloon"\
   " -device virtserialport,chardev=cloon,name=net.cloonix.0"\
   " -chardev socket,path=%s,server,nowait,id=hvc0"\
   " -device virtconsole,chardev=hvc0"\
   " -device virtio-balloon-pci,id=balloon0"\
   " -device virtio-rng-pci"

/*

   " -vga virtio -display gtk,gl=on"\
   " -vga qxl"\
   " -spice unix,addr=%s,disable-ticketing,gl=on,rendernode=/dev/dri/card0"\

*/

#define QEMU_SPICE \
   " -device qxl-vga,id=video0,ram_size=67108864,"\
   "vram_size=67108864,vram64_size_mb=0,vgamem_mb=16"\
   " -device intel-hda,id=sound0"\
   " -device hda-duplex,id=sound0-codec0,bus=sound0.0,cad=0"\
   " -device ich9-usb-ehci1,id=usb,bus=pci.0"\
   " -device ich9-usb-uhci1,masterbus=usb.0,firstport=0,bus=pci.0,multifunction=on"\
   " -chardev spicevmc,id=charredir0,name=usbredir"\
   " -device usb-redir,chardev=charredir0,id=redir0"\
   " -chardev spicevmc,id=charredir1,name=usbredir"\
   " -device usb-redir,chardev=charredir1,id=redir1"\
   " -spice unix,addr=%s,disable-ticketing"\
   " -device virtserialport,chardev=spicechannel0,name=com.redhat.spice.0"\
   " -chardev spicevmc,id=spicechannel0,name=vdagent"
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int create_linux_cmd_arm(t_vm *vm, char *linux_cmd)
{
  int i, len = 0;
  char *agent = utils_get_cdrom_path_name(vm->kvm.vm_id);
  len += sprintf(linux_cmd+len, 
                 " -m %d -name %s"
                 " -serial stdio"
                 " -nographic"
                 " -nodefaults"
                 " -pidfile %s/%s/pid"
                 " -drive file=%s,if=virtio"
                 " -append \"root=/dev/vda earlyprintk=ttyAMA0 net.ifnames=0\"",
                 vm->kvm.mem, vm->kvm.name,
                 cfg_get_work_vm(vm->kvm.vm_id),
                 DIR_UMID, vm->kvm.rootfs_used);


  len += sprintf(linux_cmd+len, 
                " -device virtio-serial-pci"
                " -chardev socket,path=%s,server,nowait,id=cloon"
                " -device virtserialport,chardev=cloon,name=net.cloonix.0"
                " -chardev socket,path=%s,server,nowait,id=hvc0"
                " -device virtconsole,chardev=hvc0"
                " -chardev socket,id=mon1,path=%s,server,nowait"
                " -mon chardev=mon1,mode=readline"
                " -chardev socket,id=qmp1,path=%s,server,nowait"
                " -mon chardev=qmp1,mode=control",
                utils_get_qbackdoor_path(vm->kvm.vm_id),
                utils_get_qhvc0_path(vm->kvm.vm_id),
                utils_get_qmonitor_path(vm->kvm.vm_id),
                utils_get_qmp_path(vm->kvm.vm_id));

  for (i=0; i<vm->kvm.nb_eth; i++)
    len+=sprintf(linux_cmd+len,"%s",format_virtkvm_net(vm,i));

  len += sprintf(linux_cmd+len, AGENT_DISK, agent);

  return len;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int create_linux_cmd_kvm(t_vm *vm, char *linux_cmd)
{
  int i, nb_cpu,  len;
  char cmd_start[3*MAX_PATH_LEN];
  char cpu_type[MAX_NAME_LEN];
  char *rootfs, *added_disk, *gname;
  char *spice_path, *cdrom;
  if (!vm)
    KOUT(" ");
  spice_path = utils_get_spice_path(vm->kvm.vm_id);
  nb_cpu = vm->kvm.cpu;
  if (inside_cloonix(&gname))
    {
    strcpy(cpu_type, "kvm64");
    }
  else
    {
    strcpy(cpu_type, "host,+vmx");
    }

  len = sprintf(cmd_start, QEMU_OPTS_BASE, 
                vm->kvm.mem,
                vm->kvm.name,
                utils_get_qmonitor_path(vm->kvm.vm_id),
                utils_get_qmp_path(vm->kvm.vm_id));

  if (!(vm->kvm.vm_config_flags & VM_CONFIG_FLAG_CISCO))
    len += sprintf(cmd_start+len, QEMU_OPTS_CLOONIX, 
                   utils_get_qbackdoor_path(vm->kvm.vm_id),
                   utils_get_qhvc0_path(vm->kvm.vm_id));

  len = sprintf(linux_cmd, " %s"
                        " -pidfile %s/%s/pid"
                        " -machine pc,accel=kvm,usb=off,dump-guest-core=off"
                        " -cpu %s"
                        " -smp %d,maxcpus=%d,cores=1",
                        cmd_start, cfg_get_work_vm(vm->kvm.vm_id),
                        DIR_UMID, cpu_type, nb_cpu, nb_cpu);
  if (spice_libs_exists())
    {
    if (!(vm->kvm.vm_config_flags & VM_CONFIG_FLAG_CISCO))
      len += sprintf(linux_cmd+len, QEMU_SPICE, spice_path);
    else
      len += sprintf(linux_cmd+len," -nographic -vga none");
    }
  if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_9P_SHARED)
    {
    if (vm->kvm.p9_host_share[0] == 0) 
      KERR(" ");
    else
      {
      if (!is_directory_readable(vm->kvm.p9_host_share))
        KERR("%s", vm->kvm.p9_host_share);
      else
        len += sprintf(linux_cmd+len, VIRTIO_9P, vm->kvm.p9_host_share,
                                                 vm->kvm.name);
      }
    }

  rootfs = vm->kvm.rootfs_used;
  added_disk = vm->kvm.added_disk;

  if  (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_NO_REBOOT)
    {
    len += sprintf(linux_cmd+len, " -no-reboot");
    }
  if  (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_INSTALL_CDROM)
    {
    len += sprintf(linux_cmd+len, INSTALL_DISK, rootfs, 0);
    len += sprintf(linux_cmd+len, ADDED_CDROM, vm->kvm.install_cdrom);
    }
  else
    {
    if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_FULL_VIRT)
      len += sprintf(linux_cmd+len, DRIVE_FULL_VIRT, rootfs, 0);
    else
      {
      if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_CISCO)
        {
        len += sprintf(linux_cmd+len, DRIVE_PARAMS_CISCO, rootfs, 0);
        len += sprintf(linux_cmd+len,
        " -uuid 3824cca6-7603-423b-8e5c-84d15d9b0a6a");
        }
      else
        len += sprintf(linux_cmd+len, DRIVE_PARAMS, rootfs, 0);
      } 
    cdrom = utils_get_cdrom_path_name(vm->kvm.vm_id);
    len += sprintf(linux_cmd+len, AGENT_CDROM, cdrom);
  
    if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_ADDED_DISK)
      len += sprintf(linux_cmd+len, DRIVE_PARAMS, added_disk, 1);
    }

  if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_ADDED_CDROM)
    {
    len += sprintf(linux_cmd+len, ADDED_CDROM, vm->kvm.added_cdrom);
    }

  for (i=0; i<vm->kvm.nb_eth; i++)
    {
    len+=sprintf(linux_cmd+len,"%s",format_virtkvm_net(vm,i));
    }
  return len;
}
/*--------------------------------------------------------------------------*/
              
/****************************************************************************/
static char *qemu_cmd_format(t_vm *vm)
{
  int len = 0;
  char *cmd = (char *) clownix_malloc(MAX_BIG_BUF, 7);
  char mach[MAX_NAME_LEN];
  char path_qemu_exe[MAX_PATH_LEN];
  char path_kern[MAX_PATH_LEN];
  char path_initrd[MAX_PATH_LEN];
  memset(cmd, 0,  MAX_BIG_BUF);
  memset(path_qemu_exe, 0, MAX_PATH_LEN);
  memset(path_kern, 0, MAX_PATH_LEN);
  memset(path_initrd, 0, MAX_PATH_LEN);
  memset(mach, 0, MAX_NAME_LEN);
  if ((vm->kvm.vm_config_flags & VM_CONFIG_FLAG_ARM) ||
      (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_AARCH64))
    {
    snprintf(path_kern, MAX_PATH_LEN-1, "%s/%s",
             cfg_get_bulk(), vm->kvm.linux_kernel);
    snprintf(path_initrd, MAX_PATH_LEN-1, "%s/%s-initramfs",
             cfg_get_bulk(), vm->kvm.linux_kernel);
    if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_AARCH64)
      {
      snprintf(path_qemu_exe, MAX_PATH_LEN-1, "%s/server/qemu/%s/%s",
               cfg_get_bin_dir(), QEMU_BIN_DIR, QEMU_AARCH64_EXE);
      snprintf(mach, MAX_NAME_LEN-1, "-M virt --cpu cortex-a57");
      }
    else
      {
      snprintf(path_qemu_exe, MAX_PATH_LEN-1, "%s/server/qemu/%s/%s",
               cfg_get_bin_dir(), QEMU_BIN_DIR, QEMU_ARM_EXE);
      snprintf(mach, MAX_NAME_LEN-1, "-M virt");
      }
    if (!file_exists(path_initrd, F_OK))
      {
      len += snprintf(cmd, MAX_BIG_BUF-1,
      "%s -L %s/server/qemu/%s %s -kernel %s",
      path_qemu_exe, cfg_get_bin_dir(), QEMU_BIN_DIR, mach, path_kern); 
      }
    else
      {
      len += snprintf(cmd, MAX_BIG_BUF-1,
      "%s -L %s/server/qemu/%s %s -kernel %s -initrd %s",
      path_qemu_exe, cfg_get_bin_dir(), QEMU_BIN_DIR,
      mach, path_kern, path_initrd); 
      }
    len += create_linux_cmd_arm(vm, cmd+len);
    }
  else
    {
    snprintf(path_qemu_exe, MAX_PATH_LEN-1, "%s/server/qemu/%s/%s",
             cfg_get_bin_dir(), QEMU_BIN_DIR, QEMU_EXE);
    len += snprintf(cmd, MAX_BIG_BUF-1, "%s -L %s/server/qemu/%s ",
                    path_qemu_exe, cfg_get_bin_dir(), QEMU_BIN_DIR);
    len += create_linux_cmd_kvm(vm, cmd+len);
    }
  return cmd;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static char *alloc_argv(char *str)
{
  int len = strlen(str);
  char *argv = (char *)clownix_malloc(len + 1, 15);
  memset(argv, 0, len + 1);
  strncpy(argv, str, len);
  return argv;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static char **create_qemu_argv(t_vm *vm)
{
  int i = 0;
  static char **argv;
  char *kvm_exe = qemu_cmd_format(vm);
  argv = (char **)clownix_malloc(10 * sizeof(char *), 13);
  memset(argv, 0, 10 * sizeof(char *));
  argv[i++] = alloc_argv(utils_get_dtach_bin_path());
  argv[i++] = alloc_argv("-n");
  argv[i++] = alloc_argv(utils_get_dtach_sock_path(vm->kvm.name));
  argv[i++] = alloc_argv("/bin/bash");
  argv[i++] = alloc_argv("-c");
  argv[i++] = kvm_exe;
  argv[i++] = NULL;
  return argv;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int start_launch_args(void *ptr)
{  

  return (utils_execve(ptr));
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void timer_launch_end(void *data)
{
  char *name = (char *) data;
  char err[MAX_PRINT_LEN];
  t_vm   *vm = cfg_get_vm(name);
  t_wake_up_eths *wake_up_eths;
  if (vm)
    {
    wake_up_eths = vm->wake_up_eths;
    if (wake_up_eths)
      {
      if (strcmp(wake_up_eths->name, name))
        KERR(" ");
      else
        {
        if (!file_exists(utils_get_dtach_sock_path(name), F_OK))
          {
          sprintf(err, "ERROR QEMU UNEXPECTED STOP %s\n", name);
          event_print(err);
          send_status_ko(wake_up_eths->llid, wake_up_eths->tid, err);
          utils_launched_vm_death(name, error_death_qemu_quiterr);
          }
        }
      }
    }
  clownix_free(data, __FUNCTION__);
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
static void launcher_death(void *data, int status, char *name)
{
  int i;
  char *time_name;
  char **argv = (char **) data;
  for (i=0; argv[i] != NULL; i++)
    clownix_free(argv[i], __FUNCTION__);
  clownix_free(argv, __FUNCTION__);
  time_name = clownix_malloc(MAX_NAME_LEN, 5);
  memset(time_name, 0, MAX_NAME_LEN);
  strncpy(time_name, name, MAX_NAME_LEN-1);
  clownix_timeout_add(500, timer_launch_end, (void *) time_name, NULL, NULL);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int launch_qemu_vm(t_vm *vm)
{
  char **argv;
  int i, pid, result = -1;
  argv = create_qemu_argv(vm);
  utils_send_creation_info(vm->kvm.name, argv);

//VIP
// gdb ...
// set follow-fork-mode child

  pid = pid_clone_launch(start_launch_args, launcher_death, NULL, 
                        (void *)argv, (void *)argv, NULL, 
                        vm->kvm.name, -1, 1);
  if (!pid)
    KERR("%s", vm->kvm.name);
  else
    {
    for (i=0; i<vm->kvm.nb_eth; i++)
      {
      if (endp_mngt_kvm_pid_clone(vm->kvm.name, i, pid))
        KERR("%s %d", vm->kvm.name, i);
      }
    }
  result = 0;

  return result;
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void timer_utils_finish_vm_init(char *name, int val)
{
  char *nm;
  nm = (char *) clownix_malloc(MAX_NAME_LEN, 9);
  memset(nm, 0, MAX_NAME_LEN);
  strncpy(nm, name, MAX_NAME_LEN-1);
  clownix_timeout_add(val, utils_finish_vm_init, (void *) nm, NULL, NULL);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void qemu_vm_automaton(void *unused_data, int status, char *name) 
{
  char err[MAX_PRINT_LEN];
  int i, state;
  t_vm   *vm = cfg_get_vm(name);
  t_wake_up_eths *wake_up_eths;
  t_small_evt vm_evt;
  if (!vm)
    return;
  wake_up_eths = vm->wake_up_eths;
  if (!wake_up_eths)
    return;
  if (strcmp(wake_up_eths->name, name))
    KOUT(" ");
  state = wake_up_eths->state;
  if (status)
    {
    sprintf(err, "ERROR when creating %s\n", name);
    event_print(err);
    send_status_ko(wake_up_eths->llid, wake_up_eths->tid, err);
    utils_launched_vm_death(name, error_death_qemuerr);
    return;
    }
  switch (state)
    {
    case auto_idle:
      wake_up_eths->state = auto_create_vm_launch;
      if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_PERSISTENT)
        clownix_timeout_add(1, static_vm_timeout, (void *) wake_up_eths,
                            NULL, NULL);
      else if (vm->kvm.vm_config_flags & VM_CONFIG_FLAG_EVANESCENT)
        derived_file_creation_request(vm);
      else
        KOUT("%X", vm->kvm.vm_config_flags);
      break;
    case auto_create_vm_launch:
      wake_up_eths->state = auto_create_vm_connect;
      for (i=0; i<vm->kvm.nb_eth; i++)
        {
        if (endp_mngt_start(0, 0, vm->kvm.name, i, endp_type_kvm))
          KERR("%s %d", vm->kvm.name, i);
        }
      if (launch_qemu_vm(vm))
        clownix_timeout_add(4000, static_vm_timeout, (void *) wake_up_eths,
                            NULL, NULL);
      else
        clownix_timeout_add(500, static_vm_timeout, (void *) wake_up_eths,
                            NULL, NULL);
      break;
    case auto_create_vm_connect:
      vm->dtach_launch = 1;
      timer_utils_finish_vm_init(name, 4000);
      qmonitor_begin_qemu_unix(name);
      qmp_begin_qemu_unix(name);
      qhvc0_begin_qemu_unix(name);
      doors_send_add_vm(get_doorways_llid(), 0, vm->kvm.name,
                        utils_get_qbackdoor_path(vm->kvm.vm_id));
      memset(&vm_evt, 0, sizeof(t_small_evt));
      strncpy(vm_evt.name, name, MAX_NAME_LEN-1);
      vm_evt.evt = vm_evt_dtach_launch_ok;
      event_subscriber_send(topo_small_event, (void *) &vm_evt);
      break;
    default:
      KOUT(" ");
    }
}
/*--------------------------------------------------------------------------*/



