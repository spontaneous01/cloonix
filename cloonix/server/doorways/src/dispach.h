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
int dispatch_get_dido_llid_with_inside_llid(int inside_llid);
char *get_g_buf(void);
void dispach_err_switch (int llid, int err);
void dispach_rx_switch(int llid, int len, char *buf);
void dispach_door_llid(int llid);
void dispach_door_end(int llid);
void dispach_door_rx(int llid, int tid, int type, int val,int len,char *buf);
int  dispach_send_to_traf_client(int llid, int val, int len, char *buf);
int dispach_send_to_openssh_client(int dido_llid, int val, int len, char *buf);
void in_rx_c2c(int inside_llid, int idx, int len, char *buf);
void in_err_gene(void *ptr, int inside_llid, int err, int from);
void dispach_init(void);
/*--------------------------------------------------------------------------*/

