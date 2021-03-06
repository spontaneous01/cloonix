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
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include "io_clownix.h"
#include "rpc_clownix.h"
#include "layout_rpc.h"
#include "doorways_sock.h"
#include "rpc_c2c.h"
#include "client_clownix.h"

static int g_llid;
static int g_connect_llid;
static t_mud_cli_dialog_cb mud_cli_dialog_cb;
static t_list_commands_cb clownix_list_commands_cb;
static t_pid_cb   clownix_pid_cb;
static t_print_cb clownix_print_cb;
static t_sys_cb   clownix_sys_cb;
static t_topo_small_event_cb clownix_topo_small_event_cb;
static t_topo_cb  clownix_topo_cb;

static t_evt_stats_endp_cb clownix_evt_stats_endp_cb;
static t_evt_stats_sysinfo_cb clownix_evt_stats_sysinfo_cb;
static t_evt_blkd_reports_cb clownix_evt_blkd_reports_cb;

static t_eventfull_cb clownix_eventfull_cb;

static t_get_path_cb clownix_get_path_cb;

#define MAX_MAIN_END_CB 0x10
static t_end_cb g_main_end_cb[MAX_MAIN_END_CB];

static char g_password[MSG_DIGEST_LEN];
static char g_doors_path[MAX_PATH_LEN];


/*****************************************************************************/
static t_hop_name_list_cb hop_name_list_cb;
static t_hop_event_cb hop_event_cb;
/*---------------------------------------------------------------------------*/



/*****************************************************************************/
int set_response_callback(t_end_cb cb, int tid)
{
  int i, new_tid;
  int choice = 0;
  int mask = 0;
  for (i=1; i<MAX_MAIN_END_CB; i++)
    {
    if (g_main_end_cb[i] == NULL)
      {
      choice = i;
      break;
      }
    }
  if (choice)
    {
    g_main_end_cb[choice] = cb;  
    mask = (choice << 24) & 0xFF000000; 
    }
  new_tid = (tid & 0xFFFFFF) | mask;
  return new_tid;
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void call_response_callback(int tid, int status, char *reason)
{
  int new_tid = (tid & 0xFFFFFF);
  int choice = (tid >> 24) & 0xFF;
  if (g_main_end_cb[choice])
    {
    g_main_end_cb[choice](new_tid, status, reason);
    g_main_end_cb[choice] = NULL;
    }
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void set_mud_cli_dialog_callback(t_mud_cli_dialog_cb cb)
{
  mud_cli_dialog_cb = cb;
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_mucli_dialog_resp(int llid, int tid, 
                            char *name, int eth, char *line, int status)
{
  if (mud_cli_dialog_cb)
    mud_cli_dialog_cb(tid, name, eth, line, status);
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_mud_cli_cmd(int tid, char *name, int eth, char *line)
{
  if (!g_llid)
    KOUT(" ");
  send_mucli_dialog_req(g_llid, tid, name, eth, line);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
int get_clownix_main_llid(void)
{
  return g_llid;
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void recv_work_dir_resp(int llid, int tid, t_topo_clc *conf)
{
  if (clownix_get_path_cb)
    clownix_get_path_cb(tid, conf);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_get_path(int tid, t_get_path_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_get_path_cb = cb;
  send_work_dir_req(g_llid, tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_status_ko(int llid, int tid, char *reason)
{
  call_response_callback(tid, -1, reason);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_status_ok(int llid, int tid, char *info)
{
  call_response_callback(tid, 0, info);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_list_pid_resp(int llid, int tid, int qty, t_pid_lst *pid)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_pid_cb)
    clownix_pid_cb(tid, qty, pid);
  clownix_pid_cb = NULL;
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif  
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
void recv_list_commands_resp(int llid, int tid, int qty, t_list_commands *list)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_list_commands_cb)
    clownix_list_commands_cb(tid, qty, list);
  clownix_list_commands_cb = NULL;
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_eventfull(int llid, int tid, int nb_endp, t_eventfull_endp *endp)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (tid != 777)
    KOUT(" ");
  if (!clownix_eventfull_cb)
    KOUT(" ");
  clownix_eventfull_cb(nb_endp, endp);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/



/*****************************************************************************/
void recv_evt_print(int llid, int tid, char *info)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_print_cb)
    clownix_print_cb(tid, info);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_topo_small_event(int llid, int tid, char *name, 
                           char *p1, char *p2, int evt)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_topo_small_event_cb)
    clownix_topo_small_event_cb(tid, name, p1, p2, evt);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_evt_stats_sysinfo(int llid, int tid, char *network_name, char *name,
                            t_stats_sysinfo *stats, char *df, int status)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_evt_stats_sysinfo_cb)
    clownix_evt_stats_sysinfo_cb(tid, name, stats, df, status);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void client_blkd_reports_sub(int tid, int sub, t_evt_blkd_reports_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_evt_blkd_reports_cb = cb;
  send_blkd_reports_sub(g_llid, tid, sub);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void recv_blkd_reports(int llid, int tid, t_blkd_reports *blkd)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_evt_blkd_reports_cb)
    clownix_evt_blkd_reports_cb(tid, blkd);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_evt_stats_endp(int llid, int tid, char *network_name, 
                         char *name, int num,
                         t_stats_counts *stats_counts, int status)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_evt_stats_endp_cb)
    clownix_evt_stats_endp_cb(tid, name, num, stats_counts, status);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_event_sys(int llid, int tid, t_sys_info *sys)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_sys_cb)
    clownix_sys_cb(tid, sys);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_event_topo(int llid, int tid, t_topo_info *topo)
{
  if (!msg_exist_channel(llid))
    KOUT(" ");
  if (clownix_topo_cb)
    clownix_topo_cb(tid, topo);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_hop_evt_doors(int llid, int tid, int flags_hop,
                        char *name, char *txt)
{
  if (hop_event_cb)
    hop_event_cb(tid, name, txt);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*--------------------------------------------------------------------------*/


/*****************************************************************************/
void client_promisc_set(int tid, t_end_cb cb, char *name, int eth, int promisc)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  if (promisc)
    send_vmcmd(g_llid, new_tid, name, 
               vmcmd_promiscious_flag_set, eth);
  else
    send_vmcmd(g_llid, new_tid, name, 
               vmcmd_promiscious_flag_unset, eth);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_req_pids(int tid, t_pid_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_pid_cb = cb;
  send_list_pid_req(g_llid, tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_list_commands(int tid,  t_list_commands_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_list_commands_cb = cb;
  send_list_commands_req(g_llid, tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
void client_req_eventfull(t_eventfull_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_eventfull_cb = cb;
  send_eventfull_sub(g_llid, 777);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_add_vm(int tid, t_end_cb cb, char *nm, 
                   int nb_eth, int vm_config_flags,
                   int cpu_qty, int mem_qty, 
                   char *kernel, char *root_fs, 
                   char *install_cdrom, char *added_cdrom, 
                   char *added_disk, char *p9_host_share,
                   t_eth_params *eth_params)
{
  t_topo_kvm kvm;
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  memset(&kvm, 0, sizeof(t_topo_kvm));
  if ((!nm) || (!root_fs))
    KOUT(" ");
  strncpy(kvm.name, nm, MAX_NAME_LEN - 1);
  kvm.vm_config_flags = vm_config_flags;
  kvm.cpu = cpu_qty;
  kvm.mem = mem_qty;
  kvm.nb_eth = nb_eth;
  if (eth_params)
    memcpy(kvm.eth_params, eth_params, MAX_ETH_VM * sizeof(t_eth_params));
  if (kernel)
    strncpy(kvm.linux_kernel, kernel, MAX_NAME_LEN - 1);
  strncpy(kvm.rootfs_input, root_fs, MAX_PATH_LEN - 1);
  if (install_cdrom)
    strncpy(kvm.install_cdrom, install_cdrom, MAX_PATH_LEN - 1);
  if (added_cdrom)
    strncpy(kvm.added_cdrom, added_cdrom, MAX_PATH_LEN - 1);
  if (added_disk)
    strncpy(kvm.added_disk, added_disk, MAX_PATH_LEN - 1);
  if (p9_host_share)
    strncpy(kvm.p9_host_share, p9_host_share, MAX_PATH_LEN - 1);
  send_add_vm(g_llid, new_tid, &kvm);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_sav_vm(int tid, t_end_cb cb, char *nm, 
                   int type, char *sav_rootfs_path)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_sav_vm(g_llid, new_tid, nm, type, sav_rootfs_path);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_sav_vm_all(int tid, t_end_cb cb,
                       int type, char *sav_rootfs_path)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_sav_vm_all(g_llid, new_tid, type, sav_rootfs_path);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_reboot_vm(int tid, t_end_cb cb, char *nm, int is_cloonix_reboot)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  if (is_cloonix_reboot)
    send_vmcmd(g_llid,new_tid,nm,vmcmd_reboot_with_cloonix_agent,0);
  else
    send_vmcmd(g_llid,new_tid,nm,vmcmd_reboot_with_qemu,0);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_halt_vm(int tid, t_end_cb cb, char *nm)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  if (!g_llid)
    KOUT(" ");
  send_vmcmd(g_llid, new_tid, nm, vmcmd_halt_with_qemu, 0);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_del_vm(int tid, t_end_cb cb, char *nm)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_vmcmd(g_llid, new_tid, nm, vmcmd_del, 0);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_add_sat(int tid, t_end_cb cb, char *name, 
                    int mutype, t_c2c_req_info *c2c_req_info)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_add_sat(g_llid, new_tid, name, mutype, c2c_req_info);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_del_sat(int tid, t_end_cb cb, char *name)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_del_sat(g_llid, new_tid, name);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_add_lan_endp(int tid, t_end_cb cb, char *name, int num, char *lan) 
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_add_lan_endp(g_llid, new_tid, name, num, lan);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_del_lan_endp(int tid, t_end_cb cb, char *name, int num, char *lan) 
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_del_lan_endp(g_llid, new_tid, name, num, lan);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_del_all(int tid, t_end_cb cb)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_del_all(g_llid, new_tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_kill_daemon(int tid, t_end_cb cb)
{
  int new_tid;
  if (!g_llid)
    KOUT(" ");
  new_tid = set_response_callback(cb, tid);
  send_kill_uml_clownix(g_llid, new_tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void client_print_sub(int tid, t_print_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_print_cb = cb;
  send_evt_print_sub(g_llid, tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_print_unsub(void)
{
  if (!g_llid)
    KOUT(" ");
  send_evt_print_unsub(g_llid, 0);
  clownix_print_cb = NULL;
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_topo_tst_sub(t_topo_cb cb)
{
  clownix_topo_cb = cb;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_topo_sub(int tid, t_topo_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_topo_cb = cb;
  send_event_topo_sub(g_llid, tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_topo_unsub(void)
{
  if (!g_llid)
    KOUT(" ");
  clownix_topo_cb = NULL;
  send_event_topo_unsub(g_llid, 0);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
} 
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_evt_stats_endp_sub(int tid, char *name, int num, int sub, 
                               t_evt_stats_endp_cb  cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_evt_stats_endp_cb = cb;
  send_evt_stats_endp_sub(g_llid, tid, name, num, sub);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif

}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_evt_stats_sysinfo_sub(int tid, char *name, int sub,
                                  t_evt_stats_sysinfo_cb  cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_evt_stats_sysinfo_cb = cb;
  send_evt_stats_sysinfo_sub(g_llid, tid, name, sub);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_topo_small_event_sub(int tid, t_topo_small_event_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_topo_small_event_cb = cb;
  send_topo_small_event_sub(g_llid, tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_topo_small_event_unsub(void)
{
  if (!g_llid)
    KOUT(" ");
  clownix_topo_small_event_cb = NULL;
  send_topo_small_event_unsub(g_llid, 0);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_sys_sub(int tid, t_sys_cb cb)
{
  if (!g_llid)
    KOUT(" ");
  clownix_sys_cb = cb;
  send_event_sys_sub(g_llid, tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
} 
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_sys_unsub(void)
{
  if (!g_llid)
    KOUT(" ");
  clownix_sys_cb = NULL;
  send_event_sys_unsub(g_llid, 0);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(g_llid);
#endif  
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void doorways_client_tx(int llid, int len, char *buf)
{
  doorways_tx(llid, 0, doors_type_switch, doors_val_none, len, buf);
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
static void rx_cb(int llid, int tid, int type, int val, int len, char *buf)
{
  if (type == doors_type_switch)
    {
    if (val == doors_val_init_link_ok)
      {
      }
    else if (val == doors_val_init_link_ko)
      {
      KOUT(" ");
      }
    else
      {
      if (doors_io_basic_decoder(llid, len, buf))
        {
        if (doors_io_c2c_decoder(llid, len, buf))
          {
          if (doors_io_layout_decoder(llid, len, buf))
            {
            if (rpct_decoder(NULL, llid, len, buf))
              KOUT("%s", buf);
            }
          }
        }
      }
    }
  else
    KOUT("%d", type);
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
#ifdef WITH_GLIB
int callback_connect_glib(void *ptr, int llid, int fd)
{
  if (llid != g_connect_llid)
    KERR("%d %d", llid, g_connect_llid);
  g_connect_llid = 0;
  if (g_llid == 0)
    {
    g_llid = glib_connect_llid(llid, fd, rx_cb, g_password);
    if (!g_llid)
      {
      printf("Cannot connect to %s\n", g_doors_path);
      KOUT("%s", g_doors_path);
      }
    if (doorways_tx(g_llid, 0, doors_type_switch,
                    doors_val_init_link, strlen("OK") , "OK"))
      {
      printf("Cannot transmit to %s\n", g_doors_path);
      KOUT("%s", g_doors_path);
      }
    }
  else
    KERR("TWO CONNECTS FOR ONE REQUEST");
  return 0;
}
/*---------------------------------------------------------------------------*/
#else
/*****************************************************************************/
static void err_cb (int llid)
{
  printf("\nREJECTED BY SERVER, CLOSED CONNECTION\n\n");
  exit(-1);
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static int callback_connect(void *ptr, int llid, int fd)
{
  (void) ptr;
  if (llid != g_connect_llid)
    KERR("%d %d", llid, g_connect_llid);
  g_connect_llid = 0;
  if (g_llid == 0)
    {
    g_llid = doorways_sock_client_inet_end(doors_type_switch, llid, fd,
                                           g_password, err_cb, rx_cb);
    if (!g_llid)
      {
      printf("Cannot connect to %s\n", g_doors_path);
      KOUT("%s", g_doors_path);
      }
    if (doorways_tx(g_llid, 0, doors_type_switch,
                    doors_val_init_link, strlen("OK") , "OK"))
      {
      printf("Cannot transmit to %s\n", g_doors_path);
      KOUT("%s", g_doors_path);
      }
    }
  else
    KERR("TWO CONNECTS FOR ONE REQUEST");
  return 0;
}
/*---------------------------------------------------------------------------*/
#endif


/****************************************************************************/
int client_is_connected(void)
{
  if (g_llid == 0)
    return 0;
  else
    return 1;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
static void timeout_connect_doors(void *data)
{
  if (g_connect_llid)
    {
    if (!client_is_connected())
      {
      close(get_fd_with_llid(g_connect_llid));
      doorways_sock_client_inet_delete(g_connect_llid);
      KERR("Timeout connection to doors  %s", g_doors_path);
      }
    }
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void client_init(char *name, char *path, char *password)
{
  int ip, port, llid;
  doorways_sock_address_detect(path, &ip, &port);
  memset(g_main_end_cb, 0, MAX_MAIN_END_CB * sizeof(t_end_cb));
  memset(g_password, 0, MSG_DIGEST_LEN);
  memset(g_doors_path, 0, MAX_PATH_LEN);
  strncpy(g_doors_path, path, MAX_PATH_LEN-1);
  strncpy(g_password, password, MSG_DIGEST_LEN-1);
  g_llid = 0;
  clownix_print_cb = NULL;
  clownix_topo_cb = NULL;
  clownix_sys_cb = NULL;
  clownix_topo_small_event_cb = NULL;
  clownix_get_path_cb = NULL;
  doors_io_basic_xml_init(doorways_client_tx);
  doors_io_c2c_init(doorways_client_tx);
  doors_io_layout_xml_init(doorways_client_tx);
  doorways_sock_init();
  msg_mngt_init(name, IO_MAX_BUF_LEN);
  rpct_redirect_string_tx(NULL, ptr_doorways_client_tx);
#ifdef WITH_GLIB
  glib_client_init();
  llid = doorways_sock_client_inet_start(ip, port, callback_connect_glib);
#else
  llid = doorways_sock_client_inet_start(ip, port, callback_connect);
#endif
  if (llid == 0)
    {
    printf("\n\nCannot connect to %s\n\n", path);
    exit(-1);
    }
  g_connect_llid = llid;
  clownix_timeout_add(300, timeout_connect_doors, NULL, NULL, NULL);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void client_loop(void)
{
  msg_mngt_loop();
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void recv_hop_name_list_doors(int llid, int tid, int nb, t_hop_list *list)
{
  if (hop_name_list_cb)
    hop_name_list_cb(nb, list);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(llid);
#endif
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
void client_set_hop_name_list(t_hop_name_list_cb cb)
{
  hop_name_list_cb = cb;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_get_hop_name_list(int tid)
{
  send_hop_get_name_list_doors(get_clownix_main_llid(), tid);
#ifdef WITH_GLIB
  glib_prepare_rx_tx(get_clownix_main_llid());
#endif
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
void client_set_hop_event(t_hop_event_cb cb)
{
  hop_event_cb = cb;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void client_get_hop_event(int tid, int flags_hop, int nb, t_hop_list *list)
{
  send_hop_evt_doors_sub(get_clownix_main_llid(), tid, flags_hop, nb, list); 
#ifdef WITH_GLIB
  glib_prepare_rx_tx(get_clownix_main_llid());
#endif
}
/*---------------------------------------------------------------------------*/








