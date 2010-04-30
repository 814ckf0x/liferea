/**
 * @file item_state.c   item state controller interface
 * 
 * Copyright (C) 2007-2010 Lars Lindner <lars.lindner@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
#include "db.h"
#include "debug.h"
#include "feedlist.h"
#include "item.h"
#include "item_state.h"
#include "itemset.h"
#include "itemlist.h"
#include "newsbin.h"
#include "node.h"
#include "vfolder.h"
#include "fl_sources/node_source.h"

static void
item_state_set_recount_flag (nodePtr node)
{
	node->needsRecount = TRUE;
}

void
item_set_flag_state (itemPtr item, gboolean newState) 
{	
	if (newState == item->flagStatus)
		return;

	node_source_item_set_flag (node_from_id (item->nodeId), item, newState);
}

void
item_flag_state_changed (itemPtr item, gboolean newState)
{
	/* 1. No propagation because we recount search folders in step 3... */

	/* 2. save state to DB */
	item->flagStatus = newState;
	db_item_state_update (item);

	/* 3. update item list GUI state */
	itemlist_update_item (item);

	/* 4. check wether we must add the item to a search folder */
	vfolder_foreach_data (vfolder_check_item, item);

	/* 5. update notification statistics */
	feedlist_reset_new_item_count ();

	/* no duplicate state propagation to avoid copies 
	   in the "Important" search folder */
}

void
item_set_read_state (itemPtr item, gboolean newState) 
{
	/* Read and update state are coupled insofar as they
	   are changed by the same user actions. So we do something
	   here if either the read state has changed or the
	   updated flag is set (which is always just reset). */
	   
	if (newState == item->readStatus && !item->updateStatus)
		return;
	
	node_source_item_mark_read (node_from_id (item->nodeId), item, newState);
}

void
item_read_state_changed (itemPtr item, gboolean newState)
{
	nodePtr node;

	debug_start_measurement (DEBUG_GUI);

	/* 1. apply to DB */
	item->readStatus = newState;
	item->updateStatus = FALSE;
	db_item_state_update (item);

	/* 2. add propagate to vfolders (must happen after changing the item state) */
	vfolder_foreach_data (vfolder_check_item, item);

	/* 3. update item list GUI state */
	itemlist_update_item (item);

	/* 4. updated feed list unread counters */
	node = node_from_id (item->nodeId);
	node_update_counters (node);

	/* 5. update notification statistics */
	feedlist_reset_new_item_count ();

	/* 6. duplicate state propagation */
	if (item->validGuid) {
		GSList *duplicates, *iter;

		duplicates = iter = db_item_get_duplicates (item->sourceId);
		while (iter) {
			itemPtr duplicate = item_load (GPOINTER_TO_UINT (iter->data));

			/* The check on node_from_id() is an evil workaround
			   to handle "lost" items in the DB that have no 
			   associated node in the feed list. This should be 
			   fixed by having the feed list in the DB too, so
			   we can clean up correctly after crashes. */
			if (duplicate && duplicate->id != item->id && node_from_id (duplicate->nodeId)) {
				item_set_read_state (duplicate, newState);
			}
			if (duplicate) item_unload (duplicate);
			iter = g_slist_next (iter);
		}
		g_slist_free (duplicates);
	}

	debug_end_measurement (DEBUG_GUI, "set read status");
}

/**
 * In difference to all the other item state handling methods
 * item_state_set_all_read does not immediately apply the 
 * changes to the GUI because it is usually called recursively
 * and would be to slow. Instead the node structure flag for
 * recounting is set. By calling feedlist_update() afterwards
 * those recounts are executed and applied to the GUI.
 */
void
itemset_mark_read (nodePtr node)
{
	itemSetPtr	itemSet;
	
	if (!node->unreadCount)
		return;
	
	itemSet = node_get_itemset (node);
	GList *iter = itemSet->ids;
	while (iter) {
		gulong id = GPOINTER_TO_UINT (iter->data);
		itemPtr item = item_load (id);
		if (item) {
			if (!item->readStatus) {
				nodePtr node;
				
				node = node_from_id (item->nodeId);
				if (node) {
					item_state_set_recount_flag (node);
					node_source_item_mark_read (node, item, TRUE);
				} else {
					g_warning ("itemset_mark_read() on lost item (id=%lu, node id=%s)!", item->id, item->nodeId);
				}

				debug_start_measurement (DEBUG_GUI);

				GSList *duplicates = db_item_get_duplicate_nodes (item->sourceId);
				GSList *duplicate = duplicates;
				while (duplicate) {
					gchar *nodeId = (gchar *)duplicate->data;
					nodePtr affectedNode = node_from_id (nodeId);
					if (affectedNode)
						item_state_set_recount_flag (affectedNode);
					g_free (nodeId);
					duplicate = g_slist_next (duplicate);
				}
				g_slist_free(duplicates);

				debug_end_measurement (DEBUG_GUI, "mark read of duplicates");
			}
			item_unload (item);
		}
		iter = g_list_next (iter);
	}
}

void
item_state_set_all_popup (const gchar *nodeId)
{
	db_itemset_mark_all_popup (nodeId);
	
	/* No GUI updating necessary... */
}
