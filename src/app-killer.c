/**
  @file app-killer.c

  Application killer program.
  
  Copyright (C) 2004-2005 Nokia Corporation.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA
*/

#include "app-killer.h"

/* Libosso context */
static osso_context_t *osso = NULL;
/* session bus connection */
static DBusConnection *ses_conn = NULL;
static DBusConnection *sys_conn = NULL;
static GMainLoop *mainloop = NULL;
static void libosso_init(void);

/**
  Send error reply to backup application.
  @param c D-BUS connection.
  @param m D-BUS message.
*/
static void send_restore_error(DBusConnection *c, DBusMessage *m)
{
    DBusMessage *err = NULL;
    dbus_bool_t rc = FALSE;
    assert(c != NULL && m != NULL);
    err = dbus_message_new_error(m, RESTORE_ERROR, NULL);
    if (err == NULL) {
        ULOG_ERR_F("dbus_message_new_error failed");
        return;
    }
    rc = dbus_connection_send(c, err, NULL);
    if (!rc) {
        ULOG_ERR_F("dbus_connection_send failed");
    }
    dbus_message_unref(err);
}

/**
  Send reply (of success) to the sender of message m.
  @param c D-BUS connection
  @param m D-BUS message to create reply for.
  @return TRUE on success, FALSE on failure.
*/
static gboolean
send_success_reply(DBusConnection *c, DBusMessage *m)
{
    DBusMessage *r = NULL;
    dbus_bool_t rc = FALSE;
    assert(c != NULL && m != NULL);
    r = dbus_message_new_method_return(m);
    if (r == NULL) {
        ULOG_ERR_F("dbus_message_new_method_return failed");
        return FALSE;
    }
    rc = dbus_connection_send(c, r, NULL);
    if (!rc) {
        ULOG_ERR_F("dbus_connection_send failed");
        dbus_message_unref(r);
        return FALSE;
    }
    dbus_message_unref(r);
    return TRUE;
}

/**
  Send error reply to locale change message.
  @param c D-BUS connection.
  @param m D-BUS message.
*/
static void send_locale_error(DBusConnection *c, DBusMessage *m)
{
    DBusMessage *err = NULL;
    dbus_bool_t rc = FALSE;
    assert(c != NULL && m != NULL);
    err = dbus_message_new_error(m, LOCALE_ERROR, NULL);
    if (err == NULL) {
        ULOG_ERR_F("dbus_message_new_error failed");
        return;
    }
    rc = dbus_connection_send(c, err, NULL);
    if (!rc) {
        ULOG_ERR_F("dbus_connection_send failed");
    }
    dbus_message_unref(err);
}

/**
  Send error reply to RFS message.
  @param c D-BUS connection.
  @param m D-BUS message.
  @param e error name.
*/
static void send_rfs_error(DBusConnection *c, DBusMessage *m,
                const char* e)
{
    DBusMessage *err = NULL;
    dbus_bool_t rc = FALSE;
    assert(c != NULL && m != NULL);
    err = dbus_message_new_error(m, e, NULL);
    if (err == NULL) {
        ULOG_ERR_F("dbus_message_new_error failed");
        return;
    }
    rc = dbus_connection_send(c, err, NULL);
    if (!rc) {
        ULOG_ERR_F("dbus_connection_send failed");
    }
    dbus_message_unref(err);
}

/**
  Sends the Exit signal for applications.
  @return TRUE on success, FALSE if the signal was not sent.
*/
static gboolean send_exit_signal()
{
    DBusMessage* m = NULL;
    dbus_bool_t ret = FALSE;
    ULOG_DEBUG_F("entering");
    m = dbus_message_new_signal(AK_BROADCAST_OP, AK_BROADCAST_IF,
                                AK_BROADCAST_EXIT);
    if (m == NULL) {
        ULOG_ERR_F("dbus_message_new_signal failed");
        return FALSE;
    }
    assert(ses_conn != NULL);
    ret = dbus_connection_send(ses_conn, m, NULL);
    if (!ret) {
        ULOG_ERR_F("dbus_connection_send failed");
        dbus_message_unref(m);
        return FALSE;
    }
    dbus_message_unref(m);
    ULOG_DEBUG_F("leaving");
    return TRUE;
}

/**
  Executes script handling RFS things.
  @return TRUE on success, FALSE on failure.
*/
static gboolean do_rfs()
{
    static char* const args[] = {AK_RFS_SCRIPT, NULL};
    int ret = exec_prog(AK_RFS_SCRIPT, args);
    if (ret) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
  Executes script handling Locale change things.
  @return TRUE on success, FALSE on failure.
*/
static gboolean do_locale_change()
{
    static char* const args[] = {AK_LOCALE_SCRIPT, NULL};
    int ret = exec_prog(AK_LOCALE_SCRIPT, args);
    if (ret) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
  Executes script handling Restore things.
  @return TRUE on success, FALSE on failure.
*/
static gboolean do_restore()
{
    static char* const args[] = {AK_RESTORE_SCRIPT, NULL};
    int ret = exec_prog(AK_RESTORE_SCRIPT, args);
    if (ret) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
  Sends a window state saving message to Task Navigator.
  Blocks until gets a reply or timeouts.
  @return TRUE on success, FALSE on error.
*/
static gboolean send_save_msg()
{
    DBusMessage *m = NULL;
    DBusMessage *reply = NULL;
    DBusError err;
    assert(ses_conn != NULL);

    m = dbus_message_new_method_call(TN_SVC, TN_OP, TN_IF, TN_SAVE_METHOD);
    if (m == NULL) {
        ULOG_ERR_F("could not create message");
        return FALSE;
    }
    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(ses_conn,
                    m, MSG_TIMEOUT, &err);
    if (reply == NULL) {
        if (dbus_error_has_name(&err, DBUS_ERROR_NO_REPLY)) {
            ULOG_ERR_F("no reply from TN received (in time)");
        } else {
            ULOG_ERR_F("error when sending message");
        }
        dbus_message_unref(m);
        return FALSE;
    }
    dbus_message_unref(m);
    return TRUE;
}

/**
  Message handler callback function for changing locale.
  This function broadcasts a D-BUS message that suggests 
  applications to exit and shuts down the base applications and
  shuts down the session bus. 
  After that, it waits for a while and then re-starts the session
  bus and base applications. Lastly, this function causes the AK
  to exit (to allow use of a shell script and simpler C code); it
  is restarted on demand by the D-BUS daemon.

  @param c D-BUS connection.
  @param m D-BUS message.
  @param data User data passed to the function.
  @return Does not return.
*/
static DBusHandlerResult 
locale_handler(DBusConnection *c, DBusMessage *m, void *data)
{
    ULOG_DEBUG_F("entered");
    ULOG_DEBUG_F("method: %s", dbus_message_get_member(m)); 

    if (!send_save_msg()) {
        ULOG_ERR_F("could not send save message "
                        "to (or no reply from) TN");
        send_locale_error(c, m);
	exit(0);
    }
    if (!send_exit_signal()) {
        ULOG_ERR_F("could not send the exit signal");
        send_locale_error(c, m);
	exit(0);
    }
    if (!do_locale_change()) {
        ULOG_ERR_F("locale change operation wasn't successful");
	/* no point to send error, no-one receives it */
	exit(0);
    }
    exit(0);
}

/**
  Message handler callback function for shutting down applications
  because of the restore operation.
  This function broadcasts a D-BUS message that suggests 
  applications to exit and shuts down the base applications and
  kills the GConf daemon.

  @param c D-BUS connection.
  @param m D-BUS message.
  @param data User data passed to the function.
  @return DBUS_HANDLER_RESULT_HANDLED
*/
static DBusHandlerResult 
restore_handler(DBusConnection *c, DBusMessage *m, void *data)
{
    ULOG_DEBUG_F("entered");
    ULOG_DEBUG_F("method: %s", dbus_message_get_member(m)); 

    if (!send_exit_signal()) {
        ULOG_ERR_F("could not send the exit signal");
        send_restore_error(c, m);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!do_restore()) {
        ULOG_ERR_F("could not shutdown programs for restore operation");
        send_restore_error(c, m);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!send_success_reply(c, m)) {
        ULOG_ERR_F("could not send reply to Backup");
        /* most probably this also fails in this situation... */
        send_restore_error(c, m);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

/**
  Message handler callback function for shutting down applications
  because of the RFS operation.
  This function broadcasts a D-BUS message that suggests 
  applications to exit, kills the GConf daemon, restarts base
  applications and D-BUS session bus. Lastly, the AK exits (to
  enable simpler C code and use of a shell script).

  @param c D-BUS connection.
  @param m D-BUS message.
  @param data User data passed to the function.
  @return Does not return
*/
static DBusHandlerResult 
rfs_shutdown_handler(DBusConnection *c, DBusMessage *m, void *data)
{
    ULOG_DEBUG_F("entered");
    ULOG_DEBUG_F("method: %s", dbus_message_get_member(m)); 

    if (!send_exit_signal()) {
        ULOG_ERR_F("could not send the exit signal");
        send_rfs_error(c, m, RFS_SHUTDOWN_ERROR);
	exit(0);
    }
    if (!do_rfs()) {
        ULOG_ERR_F("could not shut down and restart programs for RFS");
        send_rfs_error(c, m, RFS_SHUTDOWN_ERROR);
	exit(0);
    }
    exit(0);
}

/**
  System bus message filter.
*/
static
DBusHandlerResult sysbus_filter(DBusConnection* c,
                                 DBusMessage* m,
                                 void* user_data)
{
    if (dbus_message_is_signal(m, DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL,
                               "Disconnected")) {
        ULOG_CRIT("D-BUS system bus disconnected");
	exit(1);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
  Session bus message filter.
*/
static
DBusHandlerResult sesbus_filter(DBusConnection* c,
                                 DBusMessage* m,
                                 void* user_data)
{
    if (dbus_message_is_signal(m, DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL,
                               "Disconnected")) {
        ULOG_DEBUG("D-BUS session bus disconnected");
	exit(1);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
  Initialise Libosso and register object paths etc.
*/
static void libosso_init()
{
    static DBusObjectPathVTable bu_vt = { 
        .message_function = locale_handler,
        .unregister_function = NULL
    };
    dbus_bool_t rc = FALSE;
    ULOG_DEBUG("entering libosso_init");
    assert(osso == NULL); /* don't initialise Libosso twice in a row */
    assert(ses_conn == NULL);

    osso = osso_initialize(APPL_DBUS_NAME, APPL_VERSION, TRUE, NULL);
    if (osso == NULL) {
        ULOG_CRIT("Libosso initialisation failed");
        exit(1);
    }
    ses_conn = (DBusConnection*) osso_get_dbus_connection(osso);
    if (ses_conn == NULL) {
        ULOG_CRIT("osso_get_dbus_connection() failed");
        exit(1);
    }

    /* set message filters */
    if (!dbus_connection_add_filter(ses_conn, sesbus_filter, NULL, NULL)) {
        ULOG_CRIT("could not register session bus filter function");
        exit(1);
    }
    sys_conn = (DBusConnection*) osso_get_sys_dbus_connection(osso);
    if (sys_conn == NULL) {
        ULOG_CRIT("osso_get_sys_dbus_connection() failed");
        exit(1);
    }
    if (!dbus_connection_add_filter(sys_conn, sysbus_filter,
                            NULL, NULL)) {
        ULOG_CRIT("could not register system bus filter function");
        exit(1);
    }

    /* reg. locale handler */
    bu_vt.message_function = locale_handler;
    rc = dbus_connection_register_object_path(ses_conn, LOCALE_OP,
                    &bu_vt, NULL);
    if (!rc) {
        ULOG_CRIT("could not register locale handler");
        exit(1);
    }
    /* reg. restore handler */
    bu_vt.message_function = restore_handler;
    rc = dbus_connection_register_object_path(ses_conn, RESTORE_OP,
                    &bu_vt, NULL);
    if (!rc) {
        ULOG_CRIT("could not register restore handler");
        exit(1);
    }
    /* reg. RFS shutdown handler */
    bu_vt.message_function = rfs_shutdown_handler;
    rc = dbus_connection_register_object_path(ses_conn, RFS_SHUTDOWN_OP,
                    &bu_vt, NULL);
    if (!rc) {
        ULOG_CRIT("could not register RFS shutdown handler");
        exit(1);
    }
    assert(osso != NULL); 
    assert(ses_conn != NULL);
    assert(sys_conn != NULL);
}

/**
  The main function of app-killer.
  Does initialisations and goes to the Glib main loop.
*/
int main()
{
    char* ses_bus_socket = NULL;
    ULOG_OPEN(APPL_NAME);

    mainloop = g_main_loop_new(NULL, TRUE);
    ULOG_DEBUG("Glib main loop created");

    ses_bus_socket = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (ses_bus_socket == NULL) {
        ULOG_CRIT("DBUS_SESSION_BUS_SOCKET is not defined");
        exit(1);
    }

    libosso_init();

    ULOG_DEBUG("Going to the main loop");
    g_main_loop_run(mainloop); 
    ULOG_DEBUG("Returned from the main loop");
    ULOG_WARN("Glib main loop returned without a known reason");
    exit(0);
}
