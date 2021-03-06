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
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include "io_clownix.h"
#include "rpc_clownix.h"
#include "commun_consts.h"
#include "bank.h"
#include "move.h"
#include "cloonix.h"
#include "popup.h"
#include "pid_clone.h"
#include "main_timer_loop.h"
#include "menu_utils.h"
#include "menus.h"
#include "bdplot.h"

/*--------------------------------------------------------------------------*/
static int g_timeout_flipflop_freeze;
static int g_one_eventfull_has_arrived;
static int glob_eventfull_has_arrived;
/*--------------------------------------------------------------------------*/
int nb_sec_without_stats;

typedef struct t_ping_evt
{
  char name[MAX_NAME_LEN];
  int evt;
} t_ping_evt;

/*--------------------------------------------------------------------------*/
typedef struct t_chrono_act
{
  char name[MAX_NAME_LEN];
  int num;
  int is_tg;
  int switch_on;
} t_chrono_act;
/*--------------------------------------------------------------------------*/
void topo_repaint_request(void);

void eventfull_has_arrived(void)
{
  g_one_eventfull_has_arrived = 1;
  glob_eventfull_has_arrived = 0;
}


/****************************************************************************/
static int choice_pbi_flag(int ping_ok, int cga_ping_ok, int dtach_launch_ok)
{
  int result = flag_dtach_launch_ko;
  if (dtach_launch_ok)
    result = flag_dtach_launch_ok;
  if ((ping_ok) || (cga_ping_ok))
    result = flag_ping_ok;
  return result;
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
static void timer_ping_evt_node(t_bank_item *bitem, int evt)
{
  switch (evt)
    {
    case vm_evt_ping_ok:
      bitem->pbi.pbi_node->node_evt_ping_ok = 1;
      break;
    case vm_evt_ping_ko:
      bitem->pbi.pbi_node->node_evt_ping_ok = 0;
      break;
    case vm_evt_cloonix_ga_ping_ok:
      bitem->pbi.pbi_node->node_evt_cga_ping_ok = 1;
      break;
    case vm_evt_cloonix_ga_ping_ko:
      bitem->pbi.pbi_node->node_evt_cga_ping_ok = 0;
      break;
    case vm_evt_dtach_launch_ok:
      bitem->pbi.pbi_node->node_evt_dtach_launch_ok = 1;
      break;
    case vm_evt_dtach_launch_ko:
      bitem->pbi.pbi_node->node_evt_dtach_launch_ok = 0;
      break;
    default:
      KOUT(" ");
    }
  bitem->pbi.flag = choice_pbi_flag(bitem->pbi.pbi_node->node_evt_ping_ok, 
                               bitem->pbi.pbi_node->node_evt_cga_ping_ok,
                               bitem->pbi.pbi_node->node_evt_dtach_launch_ok);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void timer_ping_evt(void *data)
{
  t_bank_item *bitem;
  t_ping_evt *evt = (t_ping_evt *) data;
  if (evt->evt == c2c_evt_connection_ok)
    {
    bitem = look_for_sat_with_id(evt->name);
    if (bitem)
      bitem->pbi.pbi_sat->topo_c2c.is_peered = 1;
    }
  else if (evt->evt == c2c_evt_connection_ko)
    {
    bitem = look_for_sat_with_id(evt->name);
    if (bitem)
      bitem->pbi.pbi_sat->topo_c2c.is_peered = 0;
    }
  else
    {
    bitem = look_for_node_with_id(evt->name);
    if (bitem)
      timer_ping_evt_node(bitem, evt->evt);
    }
  clownix_free(data, __FUNCTION__);
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void ping_enqueue_evt(char *name, int evt)
{
  t_ping_evt *data = (t_ping_evt *) clownix_malloc(sizeof(t_ping_evt), 16); 
  memset(data, 0, sizeof(t_ping_evt));
  strncpy(data->name, name, MAX_NAME_LEN-1);
  data->evt = evt; 
  clownix_timeout_add(1, timer_ping_evt, (void *) data, NULL, NULL);
}
/*--------------------------------------------------------------------------*/


/*****************************************************************************/
//static void print_all_mallocs(void)
//{
//  int i;
//  unsigned long *mallocs;
//  printf("MALLOCS: ");
//  mallocs = get_clownix_malloc_nb();
//  for (i=0; i<MAX_MALLOC_TYPES; i++)
//    printf("%d:%02lu ", i, mallocs[i]);
//  printf("\n%d\n", get_nb_running_pids());
//  printf("\n%d, %d\n", get_nb_total_items(), get_max_edge_nb_per_item());
//}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void eventfull_periodic_work(void)
{
  static int count = 0;
  count++;
  if (get_nb_total_items())
    move_manager_single_step();
  if (count == 10)
    {
    count = 0;
    nb_sec_without_stats++;
    }
  topo_repaint_request();
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
gboolean refresh_request_timeout (gpointer data)
{
  popup_timeout();
  clownix_timer_beat();
  if (g_one_eventfull_has_arrived)
    {
    glob_eventfull_has_arrived++;
    if (glob_eventfull_has_arrived >= 60)
      KOUT("CONTACT LOST");
    }
  return TRUE;
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void request_move_stop_go(int stop)
{
  if (stop)
    g_timeout_flipflop_freeze = 0;
  else
    g_timeout_flipflop_freeze = 1;
}
/*---------------------------------------------------------------------------*/


/*****************************************************************************/
int get_move_freeze(void)
{
  return (!g_timeout_flipflop_freeze);
}
/*---------------------------------------------------------------------------*/

/*****************************************************************************/
void request_trace_item(int is_tg, char *name, int num)
{
  t_chrono_act *ca;
  ca = (t_chrono_act *)clownix_malloc(sizeof(t_chrono_act),16);
  memset(ca, 0, sizeof(t_chrono_act));
  strcpy(ca->name, name);  
  ca->num = num; 
  ca->is_tg = is_tg; 
  ca->switch_on = 1;
}
/*---------------------------------------------------------------------------*/


