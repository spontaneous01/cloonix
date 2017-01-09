/****************************************************************************/
/* Copyright (C) 2006-2017 Cloonix <clownix@clownix.net>  License GPL-3.0+  */
/****************************************************************************/
/*                                                                          */
/*   This program is free software: you can redistribute it and/or modify   */
/*   it under the terms of the GNU General Public License as published by   */
/*   the Free Software Foundation, either version 3 of the License, or      */
/*   (at your option) any later version.                                    */
/*                                                                          */
/*   This program is distributed in the hope that it will be useful,        */
/*   but WITHOUT ANY WARRANTY; without even the implied warranty of         */
/*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          */
/*   GNU General Public License for more details.                           */
/*                                                                          */
/*   You should have received a copy of the GNU General Public License      */
/*   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */
/*                                                                          */
/****************************************************************************/
#ifndef NO_HMAC_CIPHER

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include "io_clownix.h"
#include "channel.h"
#include "msg_layer.h"
#include "doorways_sock.h"
#include "util_sock.h"
#include "chunk.h"
#include "hmac_cipher.h"

#define MAX_TOT_LEN_WARNING_DOORWAYS_Q 100000000
#define MAX_TOT_LEN_DOORWAYS_Q 500000000
#define MAX_TOT_LEN_DOORWAYS_SOCK_Q 50000000
#define MAX_RCVBUF_SOCK 10000000


typedef struct t_rx_pktbuf
{
  char buf[MAX_DOORWAYS_BUF_LEN];
  int  offset;
  int  idx_hmac;
  int  tid;
  int  paylen;
  int  head_doors_type;
  int  val;
  int  nb_pkt_rx;
  char *payload;
} t_rx_pktbuf;


/****************************************************************************/
typedef struct t_llid
{
  int  llid;
  int  fd;
  char fct[MAX_NAME_LEN];
  t_rx_pktbuf rx_pktbuf;
  int  doors_type;
  int  nb_pkt_tx;
  char passwd[MSG_DIGEST_LEN];
  t_doorways_llid cb_llid;
  t_doorways_end cb_end;
  t_doorways_rx cb_rx;
} t_llid;
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static t_llid *g_llid_data[CLOWNIX_MAX_CHANNELS];
static int g_listen_llid_inet;
static int g_max_tx_sock_queue_len_reached;
static int g_max_tx_doorway_queue_len_reached;
static int g_init_done;
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static void fct_10_sec_timeout(void *data)
{
  if (g_max_tx_sock_queue_len_reached)
    {
    KERR("TX MAX SOCK LEN HIT: %d", g_max_tx_sock_queue_len_reached);
    g_max_tx_sock_queue_len_reached = 0;
    }
  if (g_max_tx_doorway_queue_len_reached)
    {
    KERR("DOORWAY MAX QUEUE LEN HIT: %d", g_max_tx_doorway_queue_len_reached);
    g_max_tx_doorway_queue_len_reached = 0;
    }

  clownix_timeout_add(1000, fct_10_sec_timeout, NULL, NULL, NULL);
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static void fct_stop_writing_timeout(void *data)
{
  unsigned long ul_llid = (unsigned long) data;
  channel_tx_local_flow_ctrl(NULL, (int) ul_llid, 0);
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static int max_tx_sock_queue_len_reached(int llid, int cidx, int fd)
{
  t_llid *lid = g_llid_data[llid];
  int used, max_reached = 0;
  unsigned long ul_llid = llid;
  if (!lid)
    KERR(" ");
  else
    {
    if (ioctl(fd, SIOCOUTQ, &used))
      KERR(" ");
    else
      {
      if (used > MAX_TOT_LEN_DOORWAYS_SOCK_Q) 
        {
        channel_tx_local_flow_ctrl(NULL, llid, 1);
        clownix_timeout_add(2,fct_stop_writing_timeout,(void *) ul_llid,NULL,NULL);
        g_max_tx_sock_queue_len_reached += 1;
        max_reached = 1;
        }
      }
    }
  return max_reached;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int tst_port(char *str_port, int *port)
{
  int result = 0;
  unsigned long val;
  char *endptr;
  val = strtoul(str_port, &endptr, 10);
  if ((endptr == NULL)||(endptr[0] != 0))
    result = -1;
  else
    {
    if ((val < 1) || (val > 65535))
      result = -1;
    *port = (int) val;
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static int get_ip_and_port(char *doors, int *ip, int *port)
{
  int result;
  char *ptr_ip, *ptr_port;
  ptr_ip = doors;
  ptr_port = strchr(doors, ':');
  *ptr_port = 0;
  ptr_port++;
  if (ip_string_to_int (ip, ptr_ip))
    result = -1;
  else
    result = tst_port(ptr_port, port);
  return result;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void doorways_sock_address_detect(char *doors_client_addr, int *ip, int *port)
{
  char doors[MAX_PATH_LEN];
  if (strlen(doors_client_addr) >= MAX_PATH_LEN)
    KOUT("LENGTH Problem");
  memset(doors, 0, MAX_PATH_LEN);
  *ip = 0;
  *port = 0;
  strncpy(doors, doors_client_addr, MAX_PATH_LEN-1);
  if (strchr(doors, ':'))
    {
    if (get_ip_and_port(doors, ip, port))
      KOUT("%s bad ip:port address", doors);
    }
  else
    KOUT("%s bad ip:port address", doors);
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static void clean_llid(int llid)
{
  t_llid *lid = g_llid_data[llid];
  int tx_queued;
  if (lid)
    {
    g_llid_data[llid] = NULL;
    if (!lid->cb_end)
      KOUT(" ");
    tx_queued = doorways_tx_get_tot_txq_size(llid);
    if (tx_queued)
      KERR("TX QUEUE: %d", tx_queued);
    if ((lid->rx_pktbuf.offset) || (lid->rx_pktbuf.paylen))
      KERR("RX QUEUE: %d %d", lid->rx_pktbuf.offset, lid->rx_pktbuf.paylen);
    lid->cb_end(llid);
    clownix_free(lid, __FUNCTION__);
    }
  if (msg_exist_channel(llid))
    msg_delete_channel(llid);
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static t_llid *alloc_llid(int doors_type, int llid, int fd, char *fct)
{
  int cidx, is_blkd;
  t_data_channel *dchan;
  if (!doors_type)
    KOUT("%d    %s   %s", llid, g_llid_data[llid]->fct, fct);
  if (g_llid_data[llid])
    KOUT("%d    %s   %s", llid, g_llid_data[llid]->fct, fct);
  g_llid_data[llid] = (t_llid *) clownix_malloc(sizeof(t_llid), 9);
  memset(g_llid_data[llid], 0, sizeof(t_llid));
  g_llid_data[llid]->doors_type = doors_type;
  g_llid_data[llid]->llid = llid;
  g_llid_data[llid]->fd = fd;
  strncpy(g_llid_data[llid]->fct, fct, MAX_NAME_LEN - 1);
  cidx = channel_check_llid(llid, &is_blkd, __FUNCTION__);
  dchan = get_dchan(cidx);
  memset(dchan, 0, sizeof(t_data_channel));
  dchan->decoding_state = rx_type_doorways;
  dchan->llid = llid;
  dchan->fd = fd;
  return (g_llid_data[llid]);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void set_hmac_password(int i, char *tx, int len, char *payload)
{
  int j, idx = i;
  char *md = compute_msg_digest(len, payload);
  for (j=0; j<MSG_DIGEST_LEN; j++)
    tx[idx++] = md[j];  
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int check_hmac_password(int i, char *rx, int len, char *payload)
{
  int j, k, idx, result = 0;
  char *md = compute_msg_digest(len, payload);
  idx = i;
  for (j=0; j<MSG_DIGEST_LEN; j++)
    {
    k = idx++;
    if (rx[k] != md[j]) 
      {
      result = -1;
      break;
      }
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void sock_header_set_info(char *tx,
                                 int llid, int len, int type, int val,
                                 int nb_pkt, char **ntx)
{
  int idx_hmac, i = 0;

  tx[i++] = 0xCA & 0xFF;
  tx[i++] = 0xFE & 0xFF;
  
  tx[i++] = ((llid & 0xFF00) >> 8) & 0xFF;
  tx[i++] = llid & 0xFF;
  tx[i++] = ((len & 0xFF000000) >> 24) & 0xFF;
  tx[i++] = ((len & 0xFF0000) >> 16) & 0xFF;
  tx[i++] = ((len & 0xFF00) >> 8) & 0xFF;
  tx[i++] = len & 0xFF;
  tx[i++] = ((type & 0xFF00) >> 8) & 0xFF;
  tx[i++] = type & 0xFF;
  tx[i++] = ((val & 0xFF00) >> 8) & 0xFF;
  tx[i++] = val & 0xFF;
  tx[i++] = ((nb_pkt & 0xFF00) >> 8) & 0xFF;
  tx[i++] = nb_pkt & 0xFF;
  idx_hmac = i;
  i += MSG_DIGEST_LEN;
  tx[i++] = 0xDE & 0xFF;
  tx[i++] = 0xCA & 0xFF;

  *ntx = &(tx[i++]);
  set_hmac_password(idx_hmac, tx, len, *ntx);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int sock_header_get_info(char *rx,
                                 int *llid, int *len, int *type, int *val,
                                 int *nb_pkt, char **nrx)
{
  int idx_hmac, i = 0, result=0;
  if ((rx[i++] & 0xFF) != 0xCA)
    {
    for (i=0; i<16; i++)
      printf(" %02X", (rx[i] & 0xFF));
    KERR("%02X \n", rx[0]& 0xFF);
    result = -1; 
    }
  else if ((rx[i++] & 0xFF) != 0xFE)
    {
    KERR("%02X \n", rx[1]);
    result = -1; 
    }
  else
    {
    *llid = ((rx[i] & 0xFF) << 8) + (rx[i+1] & 0xFF);
    i += 2;
    *len  = ((rx[i] & 0xFF) << 24) + ((rx[i+1] & 0xFF) << 16);
    i += 2;
    *len  += ((rx[i] & 0xFF) << 8) + (rx[i+1] & 0xFF);
    i += 2;
    *type = ((rx[i] & 0xFF) << 8) + (rx[i+1] & 0xFF);
    i += 2;
    *val  = ((rx[i] & 0xFF) << 8) + (rx[i+1] & 0xFF);
    i += 2;
    *nb_pkt  = ((rx[i] & 0xFF) << 8) + (rx[i+1] & 0xFF);
    i += 2;
    idx_hmac = i;
    i += MSG_DIGEST_LEN;
    if ((rx[i++] & 0xFF) != 0xDE)
      {
      KERR("%02X \n", rx[i-1]);
      result = -1; 
      }
    else if ((rx[i++] & 0xFF) != 0xCA)
      {
      KERR("%02X \n", rx[i-1]);
      result = -1; 
      }
    else
      {
      result = idx_hmac;
      *nrx  = &(rx[i++]);
      }
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static void err_listen_cb(void *ptr, int llid, int err, int from)
{
  KOUT(" ");
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
static void err_cb(void *ptr, int llid, int err, int from)
{
  clean_llid(llid);
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
static void cli_err_cb(void *ptr, int llid, int err, int from)
{
  clean_llid(llid);
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
static void tx_err_cb(void *ptr, int llid, int err, int from)
{
  clean_llid(llid);
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
static int tx_write(char *msg, int len, int fd)
{
  int tx_len = 0;
  tx_len = write(fd, (unsigned char *) msg, len);
  if (tx_len < 0)
    {
    if ((errno == EAGAIN) || (errno == EINTR))
      tx_len = 0;
    }
/*
  else if (tx_len > 0)
    {
    if (tx_len != len)
      KERR("%d %d", tx_len, len);
    }
*/
  return tx_len;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
static int doors_tx_send_chunk(t_data_channel *dchan, int cidx, 
                               int *correct_send, t_fd_error err_cb)
{
  int len, fstr_len, result;
  char *fstr;
  fstr_len = dchan->tx->len - dchan->tx->len_done;
  fstr = dchan->tx->chunk + dchan->tx->len_done;
  len = tx_write(fstr, fstr_len, get_fd_with_cidx(cidx));
  *correct_send = 0;
  if (len > 0)
    {
    *correct_send = len;
    if (dchan->tot_txq_size < (unsigned int) len)
      KOUT("%lu %d", dchan->tot_txq_size, len);
    dchan->tot_txq_size -= len;
    if (len == fstr_len)
      {
      first_elem_delete(&(dchan->tx), &(dchan->last_tx));
      if (dchan->tx)
        result = 1;
      else
        result = 0;
      }
    else
      {
      dchan->tx->len_done += len;
      result = 2;
      }
    }
  else if (len == 0)
    {
    result = 0;
    }
  else if (len < 0)
    {
    if (errno != EPIPE)
      KERR("%d", errno);
    dchan->tot_txq_size = 0;
    chain_delete(&(dchan->tx), &(dchan->last_tx));
    if (!err_cb)
      KOUT(" ");
    err_cb(NULL, dchan->llid, errno, 3);
    result = -1;
    }
  return result;
}
/*---------------------------------------------------------------------------*/



/*****************************************************************************/
static int tx_cb(void *ptr, int llid, int fd)
{
  int cidx, is_blkd, result, correct_send, total_correct_send = 0;
  t_data_channel *dchan;
  t_llid *lid = g_llid_data[llid];
  if (!lid)
    KERR(" ");
  else
    {
    if (lid->fd != fd)
      KOUT("%d %d", lid->fd, fd);
    cidx = channel_check_llid(llid, &is_blkd, __FUNCTION__);
    dchan = get_dchan(cidx);
    if (dchan->llid !=  llid)
      KOUT("%d %d %d %d %d", cidx, llid,  fd, dchan->llid, dchan->fd);
    if (dchan->fd !=  fd)
      KOUT(" ");
    if (channel_get_tx_queue_len(llid))
      {
      if (!dchan->tx)
        KOUT(" ");
      do
        {
        if (max_tx_sock_queue_len_reached(llid, cidx, fd))
          result = 0;
        else
          {
          result = doors_tx_send_chunk(dchan, cidx, &correct_send, tx_err_cb);
          total_correct_send += correct_send;
          }
        } while (result == 1);
      }
    }
  return total_correct_send;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
static int rx_pktbuf_fill(int *len, char  *buf, t_rx_pktbuf *rx_pktbuf)
{
  int headsize = doorways_header_size();
  int result, len_chosen, len_desired, len_avail = *len;
  if (rx_pktbuf->offset < headsize)
    {
    len_desired = headsize - rx_pktbuf->offset;
    if (len_avail >= len_desired)
      {
      len_chosen = len_desired;
      result = 1;
      }
    else
      {
      len_chosen = len_avail;
      result = 2;
      }
    }
  else
    {
    if (rx_pktbuf->paylen <= 0)
      KOUT(" ");
    len_desired = headsize + rx_pktbuf->paylen - rx_pktbuf->offset;
    if (len_avail >= len_desired)
      {
      len_chosen = len_desired;
      result = 3;
      }
    else
      {
      len_chosen = len_avail;
      result = 2;
      }
    }
  if (len_chosen + rx_pktbuf->offset > MAX_DOORWAYS_BUF_LEN)
    KOUT("%d %d", len_chosen, rx_pktbuf->offset);
  memcpy(rx_pktbuf->buf+rx_pktbuf->offset, buf, len_chosen);
  rx_pktbuf->offset += len_chosen;
  *len -= len_chosen;
  return result;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int rx_pktbuf_get_paylen(t_rx_pktbuf *rx_pktbuf)
{
  int nb_pkt, result = 0;
  rx_pktbuf->idx_hmac = sock_header_get_info(rx_pktbuf->buf, 
                                             &(rx_pktbuf->tid),
                                             &(rx_pktbuf->paylen),
                                             &(rx_pktbuf->head_doors_type),
                                             &(rx_pktbuf->val), 
                                             &(nb_pkt),
                                             &(rx_pktbuf->payload));
  if (rx_pktbuf->idx_hmac == -1)
    {
    result = -1;
    rx_pktbuf->offset = 0;
    rx_pktbuf->paylen = 0;
    rx_pktbuf->payload = NULL;
    rx_pktbuf->tid = 0;
    rx_pktbuf->head_doors_type = 0;
    rx_pktbuf->val = 0;
    }
  else
    {
    if ((nb_pkt-1) != rx_pktbuf->nb_pkt_rx)
      KERR("%d %d", nb_pkt, rx_pktbuf->nb_pkt_rx);
    if (nb_pkt == 0xFFFF)
      rx_pktbuf->nb_pkt_rx = 0;
    else
      rx_pktbuf->nb_pkt_rx = nb_pkt;
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int rx_pktbuf_process(t_llid *lid, t_rx_pktbuf *rx_pktbuf)
{
  int result = 0;
  if (!lid->cb_rx)
    KOUT(" ");
  if (rx_pktbuf->idx_hmac != 14)
    KOUT("%d", rx_pktbuf->idx_hmac);
  if (!rx_pktbuf->payload)
    KOUT(" ");
  cipher_change_key(lid->passwd);
  if (check_hmac_password(rx_pktbuf->idx_hmac, rx_pktbuf->buf,
                          rx_pktbuf->paylen, rx_pktbuf->payload))
    {
    KERR("BAD PASSWORD");
    result = -1;
    }
  else
    {
    lid->cb_rx(lid->llid, rx_pktbuf->tid, 
               rx_pktbuf->head_doors_type, rx_pktbuf->val,
               rx_pktbuf->paylen, rx_pktbuf->payload);
    }
  rx_pktbuf->offset = 0;
  rx_pktbuf->paylen = 0;
  rx_pktbuf->payload = NULL;
  rx_pktbuf->tid = 0;
  rx_pktbuf->head_doors_type = 0;
  rx_pktbuf->val = 0;
  return result;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int rx_doorways(t_llid *lid, int len, char *buf)
{
  int result = 0;
  int res, len_done, len_left_to_do = len;
  while (len_left_to_do)
    {
    len_done = len - len_left_to_do;
    res = rx_pktbuf_fill(&len_left_to_do, buf + len_done, &(lid->rx_pktbuf));
    if (res == 1)
      {
      if (rx_pktbuf_get_paylen(&(lid->rx_pktbuf)))
        {
        result = -1;
        break;
        }
      }
    else if (res == 2)
      {
      }
    else if (res == 3)
      {
      if(rx_pktbuf_process(lid, &(lid->rx_pktbuf)))
        {
        result = -1;
        break;
        }
      }
    else
      KOUT("%d", res);
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static int rx_cb(void *ptr, int llid, int fd)
{
  int result = -1;
  static char buf[MAX_DOORWAYS_BUF_LEN];
  t_llid *lid = g_llid_data[llid];
  if (!lid)
    KERR(" ");
  else
    {
    if (lid->fd != fd)
      KOUT("%d %d", lid->fd, fd);
    result = util_read(buf, MAX_DOORWAYS_BUF_LEN, fd);
    if (result < 0)
      {
      result = 0;
      clean_llid(llid);
      }
    else 
      {
      if (rx_doorways(lid, result, buf))
        KERR("%d", result);
      }
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/*****************************************************************************/
static int server_has_new_connect_from_client(void *ptr, int id, int fd)
{
  int fd_new, llid;
  char *little_name;
  t_llid *listen_lid = g_llid_data[id];
  t_llid *lid;
  if (!listen_lid)
    KERR(" ");
  else
    {
    if (listen_lid->fd != fd)
      KOUT("%d %d", listen_lid->fd, fd);
    util_fd_accept(fd, &fd_new, __FUNCTION__);
    if (fd_new > 0)
      {
      little_name = channel_get_little_name(id);
      llid = channel_create(fd_new, 0, kind_server, little_name, rx_cb,
                            tx_cb, cli_err_cb);
      if (!llid)
        KOUT(" ");
      lid = alloc_llid(doors_type_server, llid, fd_new, (char *)__FUNCTION__);
      lid->cb_end = listen_lid->cb_end;
      lid->cb_rx = listen_lid->cb_rx;
      strncpy(lid->passwd, listen_lid->passwd, MSG_DIGEST_LEN-1);
      if (!listen_lid->cb_llid)
        KOUT(" ");
      listen_lid->cb_llid(llid);
      }
    }
  return 0;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
int doorways_header_size(void)
{
  return 16 + MSG_DIGEST_LEN ;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
int doorways_sock_server_inet(int ip, int port, char *passwd, 
                              t_doorways_llid cb_llid,
                              t_doorways_end cb_end,
                              t_doorways_rx cb_rx)
{
  int llid = 0,  listen_fd;
  t_llid *listen_lid;
  if (g_init_done != 777)
    KOUT(" ");
  if (g_listen_llid_inet)
    KERR(" ");
  else
    {
    listen_fd = util_socket_listen_inet(port);
    if (listen_fd > 0)
      {
      llid = channel_create(listen_fd, 0, kind_simple_watch, "doorway_serv",
                            server_has_new_connect_from_client,
                            NULL, err_listen_cb);
      if (!llid)
        KOUT(" ");
      listen_lid = alloc_llid(doors_type_listen_server, llid, 
                              listen_fd, (char *) __FUNCTION__);
      listen_lid->cb_llid = cb_llid;
      listen_lid->cb_end = cb_end;
      listen_lid->cb_rx = cb_rx;
      strncpy(listen_lid->passwd, passwd, MSG_DIGEST_LEN-1);
      g_listen_llid_inet = llid;  
      }
    else
      KERR("%d %d", listen_fd, errno);
    }
  return llid;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
static void doorways_connect_error(void *ptr, int llid, int err, int from)
{
  if (msg_exist_channel(llid))
    channel_delete(llid);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
int doorways_sock_client_inet_start(int ip, int port, t_fd_event conn_rx)
{
  int fd, llid = 0;
  if (g_init_done != 777)
    KOUT(" ");
  fd = util_nonblock_client_socket_inet(ip, port);
  if (fd != -1)
    {
    llid = channel_create(fd, 0, kind_simple_watch_connect, "connect_wait",
                          conn_rx, conn_rx, doorways_connect_error);
    if (!llid)
      KOUT(" ");
    }
  return llid;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void doorways_sock_client_inet_delete(int llid)
{
  if (msg_exist_channel(llid))
    channel_delete(llid);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
int doorways_sock_client_inet_end(int type, int llid, int fd, 
                                  char *passwd,
                                  t_doorways_end cb_end,
                                  t_doorways_rx cb_rx)
{
  t_llid *lid;
  int err = 0, result = 0;
  unsigned int len = sizeof(err);

  if (!msg_exist_channel(llid))
    KERR("%d %d", llid, fd);
  else
    {
    channel_delete(llid); 
    if (!getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len))
      {
      if (err == 0)
        {
        result = channel_create(fd, 0, kind_client, "doorways_cli", rx_cb,
                                tx_cb, err_cb);
        if (!result)
          KOUT(" ");
        lid = alloc_llid(type, result, fd, (char *) __FUNCTION__);
        lid->cb_end = cb_end;
        lid->cb_rx = cb_rx;
        strncpy(lid->passwd, passwd, MSG_DIGEST_LEN-1);
        }
      else
        {
        util_free_fd(fd);
        }
      }
    else
      {
      KERR("%d %d", llid, fd);
      util_free_fd(fd);
      }
    }
  return result;
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
int doorways_sock_client_inet_end_glib(int type, int fd, char *passwd, 
                                        t_doorways_end cb_end,
                                        t_doorways_rx cb_rx,
                                        t_fd_event *rx_glib,
                                        t_fd_event *tx_glib) 
{
  t_llid *lid; 
  int err = 0, result = 0;
  unsigned int len = sizeof(err);
  if (!getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len))
    {
    if (err == 0)
      {
      result = channel_create(fd, 0, kind_glib_managed, "glib",
                              NULL, NULL, err_cb);
      if (!result)
        KOUT(" ");
      lid = alloc_llid(type, result, fd, (char *) __FUNCTION__);
      lid->cb_end = cb_end;
      lid->cb_rx = cb_rx;
      *rx_glib = rx_cb;
      *tx_glib = tx_cb;
      strncpy(lid->passwd, passwd, MSG_DIGEST_LEN-1);
      }
    else
      {
      KERR("%d %d", fd, err);
      util_free_fd(fd);
      }
    }
  else
    {
    KERR("%d", fd);
    util_free_fd(fd);
    }
  return result;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
static void doorways_tx_append(t_llid *lid, int cidx, t_data_channel *dchan,
                               int tid, int type, int val, int len, char *buf)
{
  char *payload;
  int headsize = doorways_header_size();
  int tot_len = len + headsize;
  long long *peak_queue_len;
  if (lid->nb_pkt_tx == 0xFFFF) 
    lid->nb_pkt_tx = 1; 
  else
    lid->nb_pkt_tx += 1; 
  cipher_change_key(lid->passwd);
  sock_header_set_info(buf, tid, len, type, val, lid->nb_pkt_tx, &payload);
  if (payload != buf + headsize)
    KOUT("%p %p", payload, buf);
  dchan->tot_txq_size += tot_len;
  peak_queue_len = get_peak_queue_len(cidx);
  if (*peak_queue_len < dchan->tot_txq_size)
    *peak_queue_len = dchan->tot_txq_size;
  chain_append_tx(&(dchan->tx), &(dchan->last_tx), tot_len, buf);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
static char *convert_and_copy(char *buf, int len)
{
  int headsize = doorways_header_size();
  char *msg_buf = (char *) clownix_malloc(len + headsize, 9);
  memset(msg_buf, 0, headsize);
  memcpy(msg_buf+headsize, buf, len);
  return msg_buf;
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
static void doorways_tx_split(t_llid *lid, int cidx, t_data_channel *dchan,
                              int tid, int type, int val, int len, char *buf)
{
  int headsize = doorways_header_size();
  int len_max = MAX_DOORWAYS_BUF_LEN - headsize;
  int len_left = len;
  int len_done = 0;
  char *msg_buf;
  while(len_left)
    {
    if (len_left <= len_max)
      {
      msg_buf = convert_and_copy(buf+len_done, len_left);
      doorways_tx_append(lid,cidx,dchan,tid,type,val,len_left,msg_buf);
      len_left = 0;
      }
    else
      {
      msg_buf = convert_and_copy(buf+len_done, len_max);
      doorways_tx_append(lid,cidx,dchan,tid,type,val,len_max,msg_buf);
      len_done += len_max;
      len_left -= len_max;
      }
    }
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
int doorways_tx_get_tot_txq_size(int llid)
{
  int result, is_blkd;
  t_data_channel *dchan;
  int cidx = channel_check_llid(llid, &is_blkd, __FUNCTION__);
  dchan = get_dchan(cidx);
  result = (int) dchan->tot_txq_size;
  return (result);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
int doorways_tx(int llid, int tid, int type, int val, int len, char *buf)
{
  int cidx, is_blkd, fd, result = -1;
  t_llid *lid;
  t_data_channel *dchan;

  if (g_init_done != 777)
    KOUT(" ");
  if ((len<0) || (len > 10*MAX_DOORWAYS_BUF_LEN))
    KOUT("%d", len);
  if (len == 0)
    return 0;


  if (!llid) 
    KOUT(" ");
  fd = get_fd_with_llid(llid);
  if (fd == -1)
    KERR(" ");
  else
    {
    lid = g_llid_data[llid];
    if (lid)
      {
      if (llid != lid->llid)
        KOUT("%d %d", llid, lid->llid);
      if (lid->doors_type != doors_type_server) 
        {
        if (lid->doors_type != type)
          {
          if (lid->doors_type == doors_type_dbssh)
            {
            if ((type != doors_type_dbssh_x11_ctrl) &&
                (type != doors_type_dbssh_x11_traf))
              KERR("%d %d", lid->doors_type, type);
            }
          else
             KERR("%d %d", lid->doors_type, type);
          }
        }
      cidx = channel_check_llid(llid, &is_blkd, __FUNCTION__);
      dchan = get_dchan(cidx);
      if (dchan->llid != lid->llid)
        KOUT(" ");
      if (dchan->tot_txq_size >  MAX_TOT_LEN_WARNING_DOORWAYS_Q)
        g_max_tx_doorway_queue_len_reached += 1;
      if (dchan->tot_txq_size <  MAX_TOT_LEN_DOORWAYS_Q)
        {
        doorways_tx_split(lid, cidx, dchan, tid, type, val, len, buf);
        result = 0;
        }
      else
        KERR("%d", (int) dchan->tot_txq_size);
      }
    else
      KERR(" ");
    }
  return result;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void ptr_doorways_client_tx(void *ptr, int llid, int len, char *buf)
{
  if (msg_exist_channel(llid))
    doorways_tx(llid, 0, doors_type_switch, doors_val_none, len, buf);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void doorways_clean_llid(int llid)
{
  clean_llid(llid);
}
/*---------------------------------------------------------------------------*/

/****************************************************************************/
void doorways_sock_init(void)
{
  cipher_init();
  g_init_done = 777;
  memset(g_llid_data, 0, sizeof(t_llid *) * CLOWNIX_MAX_CHANNELS); 
  g_listen_llid_inet = 0;
  g_max_tx_sock_queue_len_reached = 0;
  g_max_tx_doorway_queue_len_reached = 0;
  clownix_timeout_add(1000, fct_10_sec_timeout, NULL, NULL, NULL);
}
/*---------------------------------------------------------------------------*/
#endif

void doorways_linker_helper(void)
{
}
