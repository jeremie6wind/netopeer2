/**
 * @file err_netconf.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief NETCONF errors
 *
 * @copyright
 * Copyright (c) 2019 - 2021 Deutsche Telekom AG.
 * Copyright (c) 2017 - 2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */
#define _GNU_SOURCE /* asprintf */

#include "err_netconf.h"

#include <assert.h>
#include <stdio.h>

#include <sysrepo/error_format.h>

#include "common.h"
#include "compat.h"

void
np_err_sr2nc_lock_denied(sr_session_ctx_t *ev_sess, const sr_error_info_t *err_info)
{
    struct nc_session *nc_sess;
    const char *msg, *str, *ptr;
    char buf[11];

    /* message */
    msg = "Access to the requested lock is denied because the lock is currently held by another entity.";

    /* error info session ID */
    str = "DS-locked by session ";
    ptr = strstr(err_info->err[0].message, str);
    if (!ptr) {
        return;
    }
    np_get_nc_sess_by_id(atoi(ptr + strlen(str)), 0, &nc_sess);

    sprintf(buf, "%" PRIu32, nc_sess ? nc_session_get_id(nc_sess) : 0);

    /* set error */
    sr_session_set_netconf_error(ev_sess, "protocol", "lock-denied", NULL, NULL, msg, 1, "session-id", buf);
}

void
np_err_sr2nc_in_use(sr_session_ctx_t *ev_sess, const sr_error_info_t *err_info)
{
    struct nc_session *nc_sess;
    const char *msg, *str, *ptr;
    char buf[11];

    /* message */
    msg = "The request requires a resource that already is in use.";

    /* error info session ID */
    str = "DS-locked by session ";
    ptr = strstr(err_info->err[0].message, str);
    if (!ptr) {
        return;
    }
    np_get_nc_sess_by_id(atoi(ptr + strlen(str)), 0, &nc_sess);

    sprintf(buf, "%" PRIu32, nc_sess ? nc_session_get_id(nc_sess) : 0);

    /* set error */
    sr_session_set_netconf_error(ev_sess, "protocol", "in-use", NULL, NULL, msg, 1, "session-id", buf);
}

void
np_err_sr2nc_same_ds(sr_session_ctx_t *ev_sess, const char *err_msg)
{
    /* set error */
    sr_session_set_netconf_error(ev_sess, "application", "invalid-value", NULL, NULL, err_msg, 0);
}

void
np_err_missing_element(sr_session_ctx_t *ev_sess, const char *elem_name)
{
    const char *msg;

    /* message */
    msg = "An expected element is missing.";

    /* set error */
    sr_session_set_netconf_error(ev_sess, "protocol", "missing-element", NULL, NULL, msg, 1, "bad-element", elem_name);
}

void
np_err_bad_element(sr_session_ctx_t *ev_sess, const char *elem_name, const char *description)
{
    /* set error */
    sr_session_set_netconf_error(ev_sess, "protocol", "bad-element", NULL, NULL, description, 1, "bad-element", elem_name);
}

void
np_err_invalid_value(sr_session_ctx_t *ev_sess, const char *description, const char *bad_elem_name)
{
    if (bad_elem_name) {
        /* set error */
        sr_session_set_netconf_error(ev_sess, "application", "invalid-value", NULL, NULL, description, 1, "bad-element",
                bad_elem_name);
    } else {
        /* set error */
        sr_session_set_netconf_error(ev_sess, "application", "invalid-value", NULL, NULL, description, 0);
    }
}

void
np_err_ntf_sub_no_such_sub(sr_session_ctx_t *ev_sess, const char *message)
{
    /* set error */
    sr_session_set_netconf_error(ev_sess, "application", "invalid-value",
            "ietf-subscribed-notifications:no-such-subscription", NULL, message, 0);
}

void
np_err_sr2nc_edit(sr_session_ctx_t *ev_sess, const sr_session_ctx_t *err_sess)
{
    const sr_error_info_t *err_info;
    const sr_error_info_err_t *err;
    const char *ptr;
    char *path = NULL, *str = NULL;

    /* get the error */
    sr_session_get_error((sr_session_ctx_t *)err_sess, &err_info);
    assert(err_info);
    err = &err_info->err[0];

    /* get path */
    if ((ptr = strstr(err->message, "data location "))) {
        ptr += 14;
    }
    if (!ptr) {
        if ((ptr = strstr(err->message, "Schema location "))) {
            ptr += 16;
        }
    }
    if (ptr) {
        path = strndup(ptr, strlen(ptr) - 2);
    }

    if (!strncmp(err->message, "Unique data leaf(s)", 19)) {
        /* data-not-unique */
        assert(path);
        sr_session_set_netconf_error(ev_sess, "protocol", "operation-failed", "data-not-unique", NULL,
                "Unique constraint violated.", 1, "non-unique", path);
    } else if (!strncmp(err->message, "Too many", 8)) {
        /* too-many-elements */
        assert(path);
        sr_session_set_netconf_error(ev_sess, "protocol", "operation-failed", "too-many-elements", path,
                "Too many elements.", 0);
    } else if (!strncmp(err->message, "Too few", 7)) {
        /* too-few-elements */
        assert(path);
        sr_session_set_netconf_error(ev_sess, "protocol", "operation-failed", "too-few-elements", path,
                "Too few elements.", 0);
    } else if (!strncmp(err->message, "Must condition", 14)) {
        /* get the must condition error message */
        ptr = strrchr(err->message, '(');
        --ptr;
        str = strndup(err->message, ptr - err->message);

        /* must-violation */
        assert(path);
        sr_session_set_netconf_error(ev_sess, "protocol", "operation-failed", "must-violation", path, str, 0);
    } else if (!strncmp(err->message, "Invalid leafref value", 21) && strstr(err->message, "no existing target instance")) {
        /* get the value */
        assert(err->message[22] == '\"');
        ptr = strchr(err->message + 23, '\"');

        /* create error message */
        asprintf(&str, "Required leafref target with value \"%.*s\" missing.", (int)(ptr - (err->message + 23)),
                err->message + 23);

        /* instance-required */
        assert(path);
        sr_session_set_netconf_error(ev_sess, "protocol", "data-missing", "instance-required", path, str, 0);
    } else if (!strncmp(err->message, "Invalid instance-identifier", 26) && strstr(err->message, "required instance not found")) {
        /* get the value */
        assert(err->message[28] == '\"');
        ptr = strchr(err->message + 29, '\"');

        /* create error message */
        asprintf(&str, "Required instance-identifier \"%.*s\" missing.", (int)(ptr - (err->message + 29)),
                err->message + 29);

        /* instance-required */
        assert(path);
        sr_session_set_netconf_error(ev_sess, "protocol", "data-missing", "instance-required", path, str, 0);
    } else if (!strncmp(err->message, "Mandatory choice", 16)) {
        /* get choice parent */
        ptr = strrchr(path, '/');
        str = strndup(path, (ptr == path) ? 1 : ptr - path);

        /* missing-choice */
        assert(path);
        sr_session_set_netconf_error(ev_sess, "protocol", "data-missing", "mandatory-choice", str,
                "Missing mandatory choice.", 1, "missing-choice", path);
    } else {
        /* other error */
        sr_session_dup_error((sr_session_ctx_t *)err_sess, ev_sess);
    }

    free(path);
    free(str);
}
