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
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <termios.h>
#include <pty.h>
#include <sys/mman.h>


#include "io_clownix.h"
#include "rpc_clownix.h"
#include "cfg_store.h"
#include "commun_daemon.h"
#include "system_callers.h"
#include "event_subscriber.h"
#include "utils_cmd_line_maker.h"
#include "file_read_write.h"



/****************************************************************************/
void my_cp_file(char *dsrc, char *ddst, char *name)
{
  char err[MAX_PRINT_LEN];
  char src_file[MAX_PATH_LEN];
  char dst_file[MAX_PATH_LEN];
  struct stat stat_file;
  int len;
  char *buf;
  err[0] = 0;
  sprintf(src_file,"%s/%s", dsrc, name);
  sprintf(dst_file,"%s/%s", ddst, name);
  if (!stat(src_file, &stat_file))
    {
    buf = read_whole_file(src_file, &len, err);
    if (buf)
      {
      unlink(dst_file);
      if (!write_whole_file(dst_file, buf, len, err))
        chmod(dst_file, stat_file.st_mode);
      }
    clownix_free(buf, __FUNCTION__);
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void my_cp_link(char *dir_src, char *dir_dst, char *name)
{
  char link_name[MAX_PATH_LEN];
  char link_val[MAX_PATH_LEN];
  int len;
  memset(link_val, 0, MAX_PATH_LEN);
  sprintf(link_name, "%s/%s", dir_src, name);
  len = readlink(link_name, link_val, MAX_PATH_LEN-1);
  if ((len != -1) && (len < MAX_PATH_LEN-1))
    {
    sprintf(link_name, "%s/%s", dir_dst, name);
    if (symlink (link_val, link_name))
      syslog(LOG_ERR, "ERROR writing link:  %s  %d", link_name, errno);
    }
  else
    syslog(LOG_ERR, "ERROR reading link: %s  %d", link_name, errno);
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void my_mv_link(char *dir_src, char *dir_dst, char *name)
{
  char link_name[MAX_PATH_LEN];
  char link_val[MAX_PATH_LEN];
  int len;
  memset(link_val, 0, MAX_PATH_LEN);
  sprintf(link_name, "%s/%s", dir_src, name);
  len = readlink(link_name, link_val, MAX_PATH_LEN-1);
  unlink(link_name);
  if ((len != -1) && (len < MAX_PATH_LEN-1))
    {
    sprintf(link_name, "%s/%s", dir_dst, name);
    if (symlink (link_val, link_name))
      syslog(LOG_ERR, "ERROR writing link:  %s  %d", link_name, errno);
    }
  else
    syslog(LOG_ERR,"ERROR reading link: %s  %d", link_name, errno);
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void my_mv_file(char *dsrc, char *ddst, char *name)
{
  char err[MAX_PRINT_LEN];
  char src_file[MAX_PATH_LEN];
  char dst_file[MAX_PATH_LEN];
  struct stat stat_file;
  int len;
  char *buf;
  err[0] = 0;
  sprintf(src_file,"%s/%s", dsrc, name);
  sprintf(dst_file,"%s/%s", ddst, name);
  if (!stat(src_file, &stat_file))
    {
    buf = read_whole_file(src_file, &len, err);
    if (buf)
      {
      unlink(dst_file);
      if (!write_whole_file(dst_file, buf, len, err))
        chmod(dst_file, stat_file.st_mode);
      unlink(src_file);
      }
    clownix_free(buf, __FUNCTION__);
    }
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void my_mkdir(char *dst_dir)
{
  struct stat stat_file;
  if (mkdir(dst_dir, 0700))
    {
    if (errno != EEXIST)
      KOUT("%s, %d", dst_dir, errno);
    else
      {
      if (stat(dst_dir, &stat_file))
        KOUT("%s, %d", dst_dir, errno);
      if (!S_ISDIR(stat_file.st_mode))
        {
        KERR("%s", dst_dir);
        unlink(dst_dir);
        if (mkdir(dst_dir, 0700))
          KOUT("%s, %d", dst_dir, errno);
        }
      }
    }
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void my_cp_dir(char *src_dir, char *dst_dir, char *src_name, char *dst_name)
{
  DIR *dirptr;
  struct dirent *ent;
  char src_sub_dir[MAX_PATH_LEN];
  char dst_sub_dir[MAX_PATH_LEN];
  sprintf(src_sub_dir, "%s/%s", src_dir, src_name);
  sprintf(dst_sub_dir, "%s/%s", dst_dir, dst_name);
  my_mkdir(dst_sub_dir);
  dirptr = opendir(src_sub_dir);
  if (dirptr)
    {
    while ((ent = readdir(dirptr)) != NULL)
      {
      if (!strcmp(ent->d_name, "."))
        continue;
      if (!strcmp(ent->d_name, ".."))
        continue;
      if (ent->d_type == DT_REG)
        my_cp_file(src_sub_dir, dst_sub_dir, ent->d_name);
      else if(ent->d_type == DT_DIR)
        my_cp_dir(src_sub_dir, dst_sub_dir, ent->d_name, ent->d_name);
      else if(ent->d_type == DT_LNK)
        my_cp_link(src_sub_dir, dst_sub_dir, ent->d_name);
      else
        event_print("Wrong type of file %s/%s", src_sub_dir, ent->d_name);
      }
    if (closedir(dirptr))
      KOUT("%d", errno);
    }
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void my_mv_dir(char *src_dir,char *dst_dir, char *src_name,char *dst_name)
{
  DIR *dirptr;
  struct dirent *ent;
  char src_sub_dir[MAX_PATH_LEN];
  char dst_sub_dir[MAX_PATH_LEN];
  sprintf(src_sub_dir, "%s/%s", src_dir, src_name);
  sprintf(dst_sub_dir, "%s/%s", dst_dir, dst_name);
  my_mkdir(dst_sub_dir);
  dirptr = opendir(src_sub_dir);
  if (dirptr)
    {
    while ((ent = readdir(dirptr)) != NULL)
      {
      if (!strcmp(ent->d_name, "."))
        continue;
      if (!strcmp(ent->d_name, ".."))
        continue;
      if (ent->d_type == DT_REG)
        my_mv_file(src_sub_dir, dst_sub_dir, ent->d_name);
      else if(ent->d_type == DT_DIR)
        my_mv_dir(src_sub_dir, dst_sub_dir, ent->d_name, ent->d_name);
      else if(ent->d_type == DT_LNK)
        my_mv_link(src_sub_dir, dst_sub_dir, ent->d_name);
      else
        event_print("Wrong type of file %s/%s", src_sub_dir, ent->d_name);
      }
    if (closedir(dirptr))
      KOUT("%d", errno);
    }
  rmdir(src_sub_dir);
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void mk_endp_dir(void)
{
  my_mkdir(utils_get_endp_sock_dir());
  my_mkdir(utils_get_cli_sock_dir());
  my_mkdir(utils_get_snf_pcap_dir());
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
void mk_dtach_dir(void)
{
  my_mkdir(utils_get_dtach_sock_dir());
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
int mk_machine_dirs(char *name, int vm_id)
{
  char err[MAX_PRINT_LEN];
  char path[MAX_PATH_LEN];
  sprintf(path,"%s", cfg_get_work_vm(vm_id));
  my_mkdir(path);
  sprintf(path,"%s/%s", cfg_get_work_vm(vm_id), DIR_CONF);
  my_mkdir(path);
  my_mkdir(utils_dir_conf_tmp(vm_id));
  my_mkdir(utils_get_disks_path_name(vm_id));
  sprintf(path,"%s/%s", cfg_get_work_vm(vm_id),  DIR_UMID);
  my_mkdir(path);
  sprintf(path,"%s/%s", cfg_get_work_vm(vm_id),  CLOONIX_FILE_NAME);
  if (write_whole_file(path, name, strlen(name) + 1, err))
    KERR("%s", err);
  return 0;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
int check_pid_is_clownix( int pid, int vm_id)
{
  int result = 0;
  int len, fd;
  char path[MAX_PATH_LEN];
  char ndpath[MAX_PATH_LEN];
  char *ptr;
  static char cmd[MAX_BIG_BUF];
  sprintf(path, "/proc/%d/cmdline", pid);
  fd = open(path, O_RDONLY);
  if (fd > 0)
    {
    memset(cmd, 0, MAX_BIG_BUF);
    len = read(fd, cmd, MAX_BIG_BUF-2);
    ptr = cmd;
    if (len >= 0)
      {
      while ((ptr = memchr(cmd, 0, len)) != NULL)
        *ptr = ' '; 
      sprintf(ndpath,"%s", cfg_get_work_vm(vm_id));
      if (strstr(cmd, ndpath))
        result = 1;
      }
    if (close(fd))
      KOUT("%d", errno);
    }
  return result;
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
int umid_pid_already_exists(int vm_id)
{ 
  char path[MAX_PATH_LEN];
  struct stat buf;
  int result = 0;
  sprintf(path,"%s/%s/pid", cfg_get_work_vm(vm_id), DIR_UMID);
  if (!stat(path, &buf))
    result = 1;
  return result;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
int unlink_sub_dir_files_except_dir(char *dir, char *err)
{
  int result = 0;
  int end_result = 0;
  char pth[MAX_PATH_LEN];
  DIR *dirptr;
  struct dirent *ent;
  dirptr = opendir(dir);
  if (dirptr)
    {
    while ((result == 0) && ((ent = readdir(dirptr)) != NULL))
      {
      if (!strcmp(ent->d_name, "."))
        continue;
      if (!strcmp(ent->d_name, ".."))
        continue;
      sprintf(pth, "%s/%s", dir, ent->d_name);
      if(ent->d_type == DT_DIR)
        {
        sprintf(err, "%s Directory Found: %s will not delete\n", dir, pth);
        end_result = -1;
        }
      else if (unlink(pth))
        {
        sprintf(err, "File: %s could not be deleted\n", pth);
        result = -1;
        }
      }
    if (closedir(dirptr))
      KOUT("%d", errno);
    if (result == 0)
      {
      if (end_result)
        result = end_result;
      else if (rmdir(dir))
        {
        sprintf(err, "Dir: %s could not be deleted\n", dir);
        result = -1;
        }
      }
    }
  if (result)
    KERR("ERR DEL: %s %s", dir, err);
  return result;
}
/*---------------------------------------------------------------------------*/




/*****************************************************************************/
int unlink_sub_dir_files(char *dir, char *err)
{
  int result = 0;
  char pth[MAX_PATH_LEN];
  DIR *dirptr;
  struct dirent *ent;
  dirptr = opendir(dir);
  if (dirptr)
    {
    while ((result == 0) && ((ent = readdir(dirptr)) != NULL))
      {
      if (!strcmp(ent->d_name, "."))
        continue;
      if (!strcmp(ent->d_name, ".."))
        continue;
      sprintf(pth, "%s/%s", dir, ent->d_name);
      if(ent->d_type == DT_DIR)
        result = unlink_sub_dir_files(pth, err);
      else if (unlink(pth))
        {
        sprintf(err, "File: %s could not be deleted\n", pth);
        result = -1;
        }
      }
    if (closedir(dirptr))
      KOUT("%d", errno);
    if (rmdir(dir))
      {
      sprintf(err, "Dir: %s could not be deleted\n", dir);
      result = -1;
      }
    }
  return result;
}
/*---------------------------------------------------------------------------*/



/*****************************************************************************/
int rm_machine_dirs(int vm_id, char *err)
{
  int result = 0;
  char dir[MAX_PATH_LEN];
  char pth[MAX_PATH_LEN];
  DIR *dirptr;
  struct dirent *ent;
  strcpy(err, "NO_ERROR\n");
  sprintf(dir,"%s", cfg_get_work_vm(vm_id));
  dirptr = opendir(dir);
  if (dirptr)
    {
    while ((result == 0) && ((ent = readdir(dirptr)) != NULL))
      {
      if (!strcmp(ent->d_name, "."))
        continue;
      if (!strcmp(ent->d_name, ".."))
        continue;
      if (!strcmp(DIR_CONF, ent->d_name))
        {
        result = unlink_sub_dir_files(utils_dir_conf_tmp(vm_id), err);
        if (!result)
          {
          sprintf(pth,"%s/%s", cfg_get_work_vm(vm_id), DIR_CONF);
          result = unlink_sub_dir_files(pth, err);
          }
        }
      else if (!strncmp(FILE_COW, ent->d_name, strlen(FILE_COW)))
        {
        sprintf(pth,"%s/%s", cfg_get_work_vm(vm_id), ent->d_name);
        if (unlink(pth))
          {
          sprintf(err, "File: %s could not be deleted\n", pth);
          result = -1;
          }
        }
      else if (!strcmp(DIR_CLOONIX_DISKS, ent->d_name))
        {
        sprintf(pth,"%s", utils_get_disks_path_name(vm_id));
        result = unlink_sub_dir_files(pth, err);
        }
      else if (!strcmp(DIR_UMID, ent->d_name))
        {
        sprintf(pth,"%s/%s", cfg_get_work_vm(vm_id), DIR_UMID);
        result = unlink_sub_dir_files(pth, err);
        }
      else if (!strcmp(CLOONIX_FILE_NAME, ent->d_name))
        {
        sprintf(pth,"%s/%s", cfg_get_work_vm(vm_id),  CLOONIX_FILE_NAME);
        unlink(pth);
        }
      else if (!strcmp(QMONITOR_UNIX, ent->d_name))
        {
        sprintf(pth,"%s", utils_get_qmonitor_path(vm_id));
        unlink(pth);
        }
     else if (!strcmp(QMP_UNIX, ent->d_name))
        {
        sprintf(pth,"%s", utils_get_qmp_path(vm_id));
        unlink(pth);
        }
     else if (!strcmp(QHVCO_UNIX, ent->d_name))
        {
        sprintf(pth,"%s", utils_get_qhvc0_path(vm_id));
        unlink(pth);
        }
     else if (!strcmp(QBACKDOOR_UNIX, ent->d_name))
        {
        sprintf(pth,"%s", utils_get_qbackdoor_path(vm_id));
        unlink(pth);
        }
     else if (!strcmp(QBACKDOOR_HVCO_UNIX, ent->d_name))
        {
        sprintf(pth,"%s", utils_get_qbackdoor_hvc0_path(vm_id));
        unlink(pth);
        }
     else if (!strcmp(SPICE_SOCK, ent->d_name))
        {
        sprintf(pth,"%s", utils_get_spice_path(vm_id));
        unlink(pth);
        }
      else
        {
        sprintf(err, "UNEXPECTED: %s found\n", ent->d_name);
        result = -1;
        }
      }
    if (closedir(dirptr))
      KOUT("%d", errno);
    if (result == 0)
      {
      if (rmdir(dir))
        {
        sprintf(err, "Dir %s could not be deleted\n", dir);
        result = -1;
        }
      }
    }
  return result;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
int machine_read_umid_pid(int vm_id)
{
  FILE *fhd;
  char path[MAX_PATH_LEN];
  int pid;
  int result = 0;
  sprintf(path,"%s/%s/pid", cfg_get_work_vm(vm_id), DIR_UMID);
  fhd = fopen(path, "r");
  if (fhd)
    {
    if (fscanf(fhd, "%d", &pid) == 1)
      {
      if (check_pid_is_clownix( pid, vm_id))
        result = pid;
      }
    if (fclose(fhd)) 
      KOUT("%d", errno);
    }
  return result;
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
int get_pty(int *master_fdp, int *slave_fdp, char *slave_name)
{
    int  mfd, sfd = -1;
    char pty_name[MAX_NAME_LEN];
    struct termios tios;
    openpty(&mfd, &sfd, pty_name, NULL, NULL);
  if (sfd > 0)
      {
      nonblock_fd(mfd);
      event_print("pty/tty pair: got %s", pty_name);
      strcpy(slave_name, pty_name);
      chmod(slave_name, 0666);
      *master_fdp = mfd;
      *slave_fdp = sfd;
      if (tcgetattr(sfd, &tios) == 0)
        {
          tios.c_cflag &= ~(CSIZE | CSTOPB | PARENB);
          tios.c_cflag |= CS8 | CREAD | CLOCAL;
          tios.c_iflag  = IGNPAR;
          tios.c_oflag  = 0;
          tios.c_lflag  = 0;
          if (tcsetattr(sfd, TCSAFLUSH, &tios) < 0)
              event_print("couldn't set attributes on pty: %m");
        }
      else
        event_print("couldn't get attributes on pty: %m");
      return 0;
      }
  return -1;
}
/*---------------------------------------------------------------------------*/


