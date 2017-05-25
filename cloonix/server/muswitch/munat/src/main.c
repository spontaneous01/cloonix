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
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <net/if.h>
#include <errno.h>
#include <linux/if_tun.h>
#include <netinet/in.h>

#include "ioc.h"
#include "clo_tcp.h"
#include "sock_fd.h"
#include "main.h"
#include "machine.h"
#include "utils.h"
#include "bootp_input.h"
#include "packets_io.h"
#include "llid_slirptux.h"
#include "cisco.h"


static char g_resolv_dns_ip[MAX_NAME_LEN];
static char g_buf[MAX_SLIRP_RX_PROCESS];


/*****************************************************************************/
void rpct_recv_app_msg(void *ptr, int llid, int tid, char *line)
{
  char name[MAX_NAME_LEN];
  int vm_id, vm_eth;
  int m[6];
  char mac[MAX_NAME_LEN];

  DOUT(ptr, FLAG_HOP_APP, "%s", line);

  if (sscanf(line, "add_machine_mac name=%s vm_id=%d vm_eth=%d "
                   "mac=%02X:%02X:%02X:%02X:%02X:%02X", 
                   name, &vm_id, &vm_eth, &(m[0]), &(m[1]),&(m[2]),
                   &(m[3]), &(m[4]),&(m[5])) == 9) 
    {
    memset(mac, 0, MAX_NAME_LEN);
    snprintf(mac, MAX_NAME_LEN-1, "%02X:%02X:%02X:%02X:%02X:%02X", 
                                  m[0]&0xFF, m[1]&0xFF, m[2]&0xFF,
                                  m[3]&0xFF, m[4]&0xFF, m[5]&0xFF);
    set_dhcp_addr(name, vm_id, vm_eth, mac);
    }
  else if (sscanf(line, "del_machine_mac name=%s vm_id=%d vm_eth=%d "
                   "mac=%02X:%02X:%02X:%02X:%02X:%02X",
                   name, &vm_id, &vm_eth, &(m[0]), &(m[1]),&(m[2]),
                   &(m[3]), &(m[4]),&(m[5])) == 9) 
    {
    memset(mac, 0, MAX_NAME_LEN);
    snprintf(mac, MAX_NAME_LEN-1, "%02X:%02X:%02X:%02X:%02X:%02X",
                                  m[0]&0xFF, m[1]&0xFF, m[2]&0xFF,
                                  m[3]&0xFF, m[4]&0xFF, m[5]&0xFF);
    unset_dhcp_addr(name, vm_id, vm_eth, mac);
    }
  else
    KERR("%s", line);
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
char *get_glob_rx_buf(void)
{
  return g_buf;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
char *get_dns_from_resolv(void)
{
  return g_resolv_dns_ip;
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
int  tap_fd_open(t_all_ctx *all_ctx, char *tap_name)
{
  KOUT(" ");
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
int  wif_fd_open(t_all_ctx *all_ctx, char *tap_name)
{
  KOUT(" ");
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
int  raw_fd_open(t_all_ctx *all_ctx, char *tap_name)
{
  KOUT(" ");
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void rpct_recv_cli_req(void *ptr, int llid, int tid,
                    int cli_llid, int cli_tid, char *line)
{
  KERR("%s", line);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
static void socket_udp_rx(t_machine_sock *ms, int len, char *rx_buf)
{
  int tot_len = MAC_HEADER + IP_HEADER + UDP_HEADER + len;
  char *resp =  (char *) malloc(tot_len);
  char *mac = get_mac_with_name(ms->machine->name);
  if (!mac)
    KERR("%s mac not found", ms->machine->name);
  else
    {
    memcpy(&(resp[MAC_HEADER+IP_HEADER+UDP_HEADER]), rx_buf, len);
    if (!strcmp(get_dns_ip(), ms->lip))
      fill_mac_ip_header(resp, get_dns_ip(), mac);
    else
      fill_mac_ip_header(resp, get_gw_ip(), mac);
    fill_ip_ip_header((IP_HEADER + UDP_HEADER + len), &(resp[MAC_HEADER]),
                       ms->lip, ms->fip, IPPROTO_UDP);
    fill_udp_ip_header((UDP_HEADER + len), &(resp[MAC_HEADER+IP_HEADER]),
                        ms->lport, ms->fport);
    packet_output_to_slirptux(tot_len, resp);
    }
  free(resp);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
int rx_udp_so(void *ptr, int llid, int fd)
{
  int result = 0;
  char *buf = get_glob_rx_buf();
  t_machine_sock *ms;
  struct sockaddr_in rx_addr;
  socklen_t  cliLen = sizeof(rx_addr);
  ms = get_llid_to_socket(llid);
  if (!ms)
    KOUT(" ");
  if (ms->fd != fd)
    KOUT(" ");
  result = recvfrom(fd, buf, MAX_SLIRP_RX_PROCESS, 0,
                    (struct sockaddr *) &rx_addr, &cliLen);
  socket_udp_rx(ms, result, buf);
  return result;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void err_udp_so(void *ptr, int llid, int err, int from)
{
  int is_blkd;
  t_machine_sock *ms;
  ms = get_llid_to_socket(llid);
  if (ms)
    {
    free_machine_sock(ms);
    }
  else
    {
    KERR(" ");
    if (msg_exist_channel(get_all_ctx(), llid, &is_blkd, __FUNCTION__))
      msg_delete_channel(get_all_ctx(), llid);
    }
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
int rx_from_traffic_sock(t_all_ctx *all_ctx, int tidx, t_blkd *bd)
{
  if (bd)
    {
    packet_input_from_slirptux(bd->payload_len, bd->payload_blkd);
    blkd_free((void *) all_ctx, bd);
    }
  return 0;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
static void get_dns_addr(char *dns_ip)
{
  uint32_t dns_addr = 0;
  char buff[512];
  char buff2[256];
  char *ptr_start, *ptr_end;
  FILE *f;
  f = fopen("/etc/resolv.conf", "r");
  if (f)
    {
    while (fgets(buff, 512, f) != NULL)
      {
      if (sscanf(buff, "nameserver%*[ \t]%256s", buff2) == 1)
        {
        ptr_start = buff2 + strspn(buff2, " \r\n\t");
        ptr_end = ptr_start + strcspn(ptr_start, " \r\n\t");
        if (ptr_end)
          *ptr_end = 0;
        strcpy(dns_ip, ptr_start);
        if (ip_string_to_int (&dns_addr, dns_ip))
          continue;
        else
          break;
        }
      }
    }
  if (!dns_addr)
    strcpy(dns_ip, "127.0.0.1");
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
int main (int argc, char *argv[])
{
  t_all_ctx *all_ctx;
  int nat_type;
  char *endptr;
  if (argc != 5)
    KOUT(" ");
  nat_type = strtoul(argv[4], &endptr, 10);
  if ((endptr == NULL)||(endptr[0] != 0))
    KOUT(" ");
  if (nat_type != endp_type_nat)
    KOUT("%d", nat_type);
  cloonix_set_pid(getpid());
  all_ctx = msg_mngt_init((char *) argv[2], 0, IO_MAX_BUF_LEN);
  blkd_set_our_mutype((void *) all_ctx, nat_type);
  strncpy(all_ctx->g_net_name, argv[1], MAX_NAME_LEN-1);
  strncpy(all_ctx->g_name, argv[2], MAX_NAME_LEN-1);
  strncpy(all_ctx->g_path, argv[3], MAX_PATH_LEN-1);
  sock_fd_init(all_ctx);
  clownix_real_timer_init(all_ctx);
  clo_init(all_ctx, llid_clo_low_output,
           llid_clo_data_rx_possible, llid_clo_data_rx, 
           llid_clo_high_syn_rx, llid_clo_high_synack_rx, 
           llid_clo_high_close_rx);
  get_dns_addr(g_resolv_dns_ip);
  init_utils("172.17.0.3","172.17.0.2", "172.17.0.1");
  init_bootp();
  packets_io_init();
  machine_init();
  llid_slirptux_tcp_init();
  cisco_init(all_ctx);
  msg_mngt_loop(all_ctx);
  return 0;
}
/*--------------------------------------------------------------------------*/

