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
t_llid_rank *llid_rank_get_with_name(char *name);
t_llid_rank *llid_rank_get_with_llid(int llid);
t_llid_rank *llid_rank_get_with_prechoice_rank(uint32_t rank);
t_llid_rank *llid_rank_peer_rank_set(int llid, char *name, int num, 
                                     uint32_t rank);
int llid_rank_peer_rank_unset(int llid, char *name, uint32_t rank);
void llid_rank_llid_create(int llid, char *name, uint32_t rank);
/*---------------------------------------------------------------------------*/
void llid_rank_sig_disconnect(t_all_ctx *all_ctx, int llid);
void llid_rank_traf_disconnect(t_all_ctx *all_ctx, int llid);
int  llid_rank_traf_connect(t_all_ctx *all_ctx, int llid, char *buf);
int get_llid_traf_tab(t_all_ctx *all_ctx, int llid, int **llid_tab);
/*---------------------------------------------------------------------------*/
void init_llid_rank(void);
/*---------------------------------------------------------------------------*/
