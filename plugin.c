/**
 * @file plugin.c
 * Connects the plugin's functionality to all the important Pidgin interfaces
 *
 * @section LICENSE
 * Copyright (C) 2012 David Michael <fedora.dm0@gmail.com>
 *
 * This file is part of Window Merge.
 *
 * Window Merge is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Window Merge is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Window Merge.  If not, see <http://www.gnu.org/licenses/>.
**/

#include "plugin.h"

#include <gtkblist.h>
#include <gtkconv.h>
#include <gtkplugin.h>

#include <pluginpref.h>
#include <prefs.h>
#include <version.h>

#include "window_merge.h"

/**
 * A preference callback to reconstruct the layout panes when settings change
 *
 * @param[in] name       Unused
 * @param[in] type       Unused
 * @param[in] pvalue     Pointer to the value of the preference
 * @param[in] data       Unused
**/
static void pref_convs_side_cb(U const char *name, U PurplePrefType type,
			       gconstpointer pvalue, U gpointer data)
{
	PidginBuddyList *gtkblist; /*< The Buddy List being restructured */

	/* XXX: There should be an interface to list available Buddy List
	 * windows. */
	gtkblist = pidgin_blist_get_default_gtk_blist();

	pwm_create_paned_layout(gtkblist, pvalue);
}

/**
 * A callback for when a conversation is opened
 *
 * This will simply remove the instructions tab when a conversation is opened
 * in a notebook that is displaying it.
 *
 * @param[in] conv       The new conversation
**/
static void conversation_created_cb(PurpleConversation *conv)
{
	PidginConversation *gtkconv; /*< The new Pidgin conversation */
	PidginBuddyList *gtkblist; /*< The Buddy List associated with conv */
	PidginWindow *gtkconvwin; /*< The conversation window that owns conv */

	if (conv == NULL)
		return;

	gtkconv = PIDGIN_CONVERSATION(conv);
	gtkconvwin = pidgin_conv_get_window(gtkconv);
	gtkblist = pwm_convs_get_blist(gtkconvwin);

	/* Sanity check: This callback should only continue for merged windows.
	 */
	if (gtkblist == NULL)
		return;

	/* If there is a tab in addition to the instructions tab, remove it. */
	if (pidgin_conv_window_get_gtkconv_count(gtkconvwin) > 1) {
		pwm_hide_dummy_conversation(gtkblist);
		pwm_set_conv_menus_visible(gtkblist, TRUE);

		/* Process queued focus events, and focus the conversation entry
		 * field. */
		while (gtk_events_pending())
			gtk_main_iteration();
		gtk_widget_grab_focus(gtkconv->entry);
	}
}

/**
 * A callback for when a conversation is being closed
 *
 * This is only used to display help, hide conversation menu items, and reset
 * the window title when the last conversation in the Buddy List window is
 * being closed.
 *
 * @param[in] conv       The conversation on its way out the door
**/
static void deleting_conversation_cb(PurpleConversation *conv)
{
	PidginBuddyList *gtkblist; /*< The Buddy List associated with conv */
	PidginWindow *gtkconvwin; /*< The conversation window that owns conv */

	if (conv == NULL)
		return;

	gtkconvwin = pidgin_conv_get_window(PIDGIN_CONVERSATION(conv));
	gtkblist = pwm_convs_get_blist(gtkconvwin);

	/* Sanity check: This callback should only continue for merged windows.
	 */
	if (gtkblist == NULL)
		return;

	/* If the last conv is being deleted, reset help, icons, title, and
	 * menu. */
	if (pidgin_conv_window_get_gtkconv_count(gtkconvwin) <= 1) {
		pwm_show_dummy_conversation(gtkblist);
		gtk_window_set_icon_list(GTK_WINDOW(gtkblist->window), NULL);
		gtk_window_set_title(GTK_WINDOW(gtkblist->window),
				     pwm_fetch(gtkblist, "title"));
		pwm_set_conv_menus_visible(gtkblist, FALSE);
	}
}

/**
 * A callback for when a conversation tab is being dragged out of its window
 *
 * The only purpose this serves is to treat dragging a conversation out of a
 * Buddy List as if it was being deleted, in terms of when to create a dummy
 * conversation tab.
 *
 * @param[in] src        The window from which a conversation is being dragged
 * @param[in] dst        The window where a conversation is being dropped
**/
static void conversation_dragging_cb(PidginWindow *src, PidginWindow *dst)
{
	if (src != dst && pwm_convs_get_blist(src) != NULL)
		deleting_conversation_cb(
		    pidgin_conv_window_get_active_conversation(src));
}

/**
 * A callback for when a conversation is hidden (moved to a hidden window)
 *
 * This simply treats the conversation as if it was deleted, or dragged out of
 * its parent window.  The dummy conversation's tab needs to be handled in the
 * same manner for this case.
 *
 * @param[in] gtkconv    The conversation that is being hidden
**/
static void conversation_hiding_cb(PidginConversation *gtkconv)
{
	if (gtkconv != NULL)
		deleting_conversation_cb(gtkconv->active_conv);
}

/**
 * A callback for when the selected tab changes
 *
 * The important cases caught by this function are when conversations are
 * dragged into a window displaying the instructions tab and automatically
 * selected.  The event will just be handled as if the conversation was newly
 * created in that window.
 *
 * This signal is used because there is no "conversation-dragged".  The signal
 * for dragging is emitted before the conversation is moved, so everything
 * would explode if the instruction label's tab is removed at that point.  This
 * is executed to remove it after a real conversation was moved and selected.
 *
 * @param[in] conv       The new active conversation
**/
static void conversation_switched_cb(PurpleConversation *conv)
{
	conversation_created_cb(conv);
}

/**
 * A callback for when a Buddy List is created to merge a conv window with it
 *
 * @param[in] blist      The Buddy List that was created
**/
static void gtkblist_created_cb(U PurpleBuddyList *blist)
{
	pwm_merge_conversation(PIDGIN_BLIST(blist));
}

/**
 * A conversation placement function to attach convs to the default Buddy List
 *
 * @param[in] gtkconv    Pointer to a new conversation GUI needing to be placed
**/
static void conv_placement_by_blist(PidginConversation *gtkconv)
{
	PidginBuddyList *
	    gtkblist; /*< The default Buddy List, to own the conv  */
	PidginWindow *gtkconvwin; /*< The Buddy List's associated conv window */

	gtkblist = pidgin_blist_get_default_gtk_blist();
	gtkconvwin = pwm_blist_get_convs(gtkblist);

	if (gtkconvwin != NULL)
		pidgin_conv_window_add_gtkconv(gtkconvwin, gtkconv);

	/* XXX: A fallback placement avoids segfaults after the plugin's
	 * disabled. */
	else
		pidgin_conv_placement_get_fnc("last")(gtkconv);
}

/**
 * The plugin's load function
 *
 * @param[in] plugin     Pointer to the plugin
 * @return               Whether to continue loading
**/
static gboolean plugin_load(PurplePlugin *plugin)
{
	PidginBuddyList *
	    gtkblist;	  /*< For determining if blist was initialized */
	void *conv_handle;     /*< The conversations handle                 */
	void *gtkblist_handle; /*< The Pidgin Buddy List handle             */
	void *gtkconv_handle;  /*< The Pidgin conversations handle          */

	/* XXX: There should be an interface to list available Buddy List
	 * windows. */
	gtkblist = pidgin_blist_get_default_gtk_blist();
	conv_handle = purple_conversations_get_handle();
	gtkblist_handle = pidgin_blist_get_handle();
	gtkconv_handle = pidgin_conversations_get_handle();

	/* Add the conversation placement option provided by this plugin. */
	pidgin_conv_placement_add_fnc(PLUGIN_TOKEN, _(PWM_STR_CP_BLIST),
				      &conv_placement_by_blist);
	purple_prefs_trigger_callback(PIDGIN_PREFS_ROOT
				      "/conversations/placement");

	/* Rebuild the layout when the preference changes. */
	purple_prefs_connect_callback(plugin, PREF_SIDE, pref_convs_side_cb,
				      NULL);

	/* Toggle the instruction panel as conversations come and go. */
	purple_signal_connect(conv_handle, "conversation-created", plugin,
			      PURPLE_CALLBACK(conversation_created_cb), NULL);
	purple_signal_connect(conv_handle, "deleting-conversation", plugin,
			      PURPLE_CALLBACK(deleting_conversation_cb), NULL);
	purple_signal_connect(gtkconv_handle, "conversation-dragging", plugin,
			      PURPLE_CALLBACK(conversation_dragging_cb), NULL);
	purple_signal_connect(gtkconv_handle, "conversation-hiding", plugin,
			      PURPLE_CALLBACK(conversation_hiding_cb), NULL);
	purple_signal_connect(gtkconv_handle, "conversation-switched", plugin,
			      PURPLE_CALLBACK(conversation_switched_cb), NULL);

	/* Hijack Buddy Lists as they are created. */
	purple_signal_connect(gtkblist_handle, "gtkblist-created", plugin,
			      PURPLE_CALLBACK(gtkblist_created_cb), NULL);

	/* If a default Buddy List is already available, use it immediately. */
	if (gtkblist != NULL && gtkblist->window != NULL)
		pwm_merge_conversation(gtkblist);

	return TRUE;
}

/**
 * The plugin's unload function
 *
 * @param[in] plugin     Unused
 * @return               Whether to continue unloading
**/
static gboolean plugin_unload(U PurplePlugin *plugin)
{
	/* Remove the conversation placement option. */
	pidgin_conv_placement_remove_fnc(PLUGIN_TOKEN);
	purple_prefs_trigger_callback(PIDGIN_PREFS_ROOT
				      "/conversations/placement");

	/* XXX: There should be an interface to list available Buddy List
	 * windows. */
	pwm_split_conversation(pidgin_blist_get_default_gtk_blist());

	return TRUE;
}

/**
 * Create the plugin's preferences window
 *
 * @param[in] plugin     Unused
 * @return               Pointer to the newly created preferences window
**/
static PurplePluginPrefFrame *get_plugin_pref_frame(U PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame; /*< The plugin preferences window frame */
	PurplePluginPref *ppref; /*< A plugin preference option               */

	frame = purple_plugin_pref_frame_new();

	/* TRANSLATORS: This is the name of the plugin preference for deciding
	   the
	   orientation of the panes created for the Buddy List and
	   conversations. */
	ppref = purple_plugin_pref_new_with_name_and_label(
	    PREF_SIDE, _(""
			 "Conversation pane location"));
	purple_plugin_pref_set_type(ppref, PURPLE_PLUGIN_PREF_CHOICE);

	/* TRANSLATORS: This value means: Buddy List left, conversations right.
	 */
	purple_plugin_pref_add_choice(ppref, _("Right of Buddy List"), "right");

	/* TRANSLATORS: This value means: conversations left, Buddy List right.
	 */
	purple_plugin_pref_add_choice(ppref, _("Left of Buddy List"), "left");

	/* TRANSLATORS: This value means: conversations top, Buddy List bottom.
	 */
	purple_plugin_pref_add_choice(ppref, _("Above Buddy List"), "top");

	/* TRANSLATORS: This value means: Buddy List top, conversations bottom.
	 */
	purple_plugin_pref_add_choice(ppref, _("Below Buddy List"), "bottom");

	purple_plugin_pref_frame_add(frame, ppref);

	return frame;
}

/**
 * The plugin API's UI structure for registering the preferences window
**/
static PurplePluginUiInfo prefs_info = {get_plugin_pref_frame, 0, NULL,

					NULL, NULL, NULL, NULL};

/**
 * The plugin's init function, used to define plugin preferences
 *
 * @param[in] plugin     The libpurple plugin structure being initialized
**/
static void plugin_init(PurplePlugin *plugin)
{
#ifdef ENABLE_NLS
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
#endif
	plugin->info->name = _(plugin->info->name);
	plugin->info->summary = _(plugin->info->summary);
	plugin->info->description = _(plugin->info->description);
	plugin->info->author = _(plugin->info->author);

	/* Initialize the root of the plugin's preferences path. */
	purple_prefs_add_none(PREF_ROOT);

	/* Set the default size of the Buddy List in vertical panes. */
	purple_prefs_add_int(PREF_HEIGHT, 300);

	/* Set the default size of the Buddy List in horizontal panes. */
	purple_prefs_add_int(PREF_WIDTH, 300);

	/* Set the default side of the Buddy List window to attach
	 * conversations. */
	purple_prefs_add_string(PREF_SIDE, "right");
}

/**
 * The plugin information to be fed to the plugin API
**/
static PurplePluginInfo info = {
    PURPLE_PLUGIN_MAGIC, PURPLE_MAJOR_VERSION, PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_STANDARD, PIDGIN_PLUGIN_TYPE, 0, NULL,
    PURPLE_PRIORITY_DEFAULT,

    PLUGIN_ID, PLUGIN_NAME, PLUGIN_VERSION,

    /* TRANSLATORS: As the plugin summary, this should be able to describe what
       the plugin does in one sentence. */
    N_("Merges the Buddy List window with a conversation window"),

    /* TRANSLATORS: This is the plugin description, so you can use as much
       writing as needed to describe what the plugin can do. */
    N_("Enabling this plugin will allow conversations to be attached to the "
       "Buddy List window.  Preferences are available to customize the "
       "plugin's "
       "panel layout."),

    /* TRANSLATORS: This is a proper name, so you shouldn't have to modify it
       unless you are transliterating to a non-Latin script. */
    N_("David Michael <fedora.dm0@gmail.com>"),

    PLUGIN_URL,

    plugin_load, plugin_unload, NULL,

    NULL, NULL, &prefs_info, NULL,

    NULL, NULL, NULL, NULL};

PURPLE_INIT_PLUGIN(PLUGIN_TOKEN, plugin_init, info)
