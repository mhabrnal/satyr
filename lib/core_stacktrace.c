/*
    core_stacktrace.c

    Copyright (C) 2012  Red Hat, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "core_stacktrace.h"
#include "core_frame.h"
#include "core_thread.h"
#include "gdb_stacktrace.h"
#include "gdb_frame.h"
#include "gdb_thread.h"
#include "location.h"
#include "normalize.h"
#include "utils.h"
#include "strbuf.h"
#include "unstrip.h"
#include "json.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

struct sr_core_stacktrace *
sr_core_stacktrace_new()
{
    struct sr_core_stacktrace *stacktrace =
        sr_malloc(sizeof(struct sr_core_stacktrace));

    sr_core_stacktrace_init(stacktrace);
    return stacktrace;
}

void
sr_core_stacktrace_init(struct sr_core_stacktrace *stacktrace)
{
    stacktrace->signal = 0;
    stacktrace->executable = NULL;
    stacktrace->crash_thread = NULL;
    stacktrace->threads = NULL;
}

void
sr_core_stacktrace_free(struct sr_core_stacktrace *stacktrace)
{
    if (!stacktrace)
        return;

    while (stacktrace->threads)
    {
        struct sr_core_thread *thread = stacktrace->threads;
        stacktrace->threads = thread->next;
        sr_core_thread_free(thread);
    }

    free(stacktrace);
}

struct sr_core_stacktrace *
sr_core_stacktrace_dup(struct sr_core_stacktrace *stacktrace)
{
    struct sr_core_stacktrace *result = sr_core_stacktrace_new();
    memcpy(result, stacktrace, sizeof(struct sr_core_stacktrace));

    if (stacktrace->threads)
        result->threads = sr_core_thread_dup(stacktrace->threads, true);

    return result;
}

int
sr_core_stacktrace_get_thread_count(struct sr_core_stacktrace *stacktrace)
{
    struct sr_core_thread *thread = stacktrace->threads;
    int count = 0;
    while (thread)
    {
        thread = thread->next;
        ++count;
    }

    return count;
}

struct sr_core_thread *
sr_core_stacktrace_find_crash_thread(struct sr_core_stacktrace *stacktrace)
{
    /* If there is no thread, be silent and report NULL. */
    if (!stacktrace->threads)
        return NULL;

    /* If there is just one thread, it is simple. */
    if (!stacktrace->threads->next)
        return stacktrace->threads;

    struct sr_core_thread *thread = stacktrace->threads;
    while (thread)
    {
        if (sr_core_thread_find_exit_frame(thread))
            return thread;

        thread = thread->next;
    }

    return thread;
}

struct sr_core_stacktrace *
sr_core_stacktrace_from_json(struct sr_json_value *root,
                             char **error_message)
{
    if (root->type != SR_JSON_OBJECT)
    {
        *error_message = sr_strdup("Invalid type of root value; object expected.");
        return NULL;
    }

    struct sr_core_stacktrace *result = sr_core_stacktrace_new();

    /* Read signal. */
    for (unsigned i = 0; i < root->u.object.length; ++i)
    {
        if (0 != strcmp("signal", root->u.object.values[i].name))
            continue;

        if (root->u.object.values[i].value->type != SR_JSON_INTEGER)
        {
            *error_message = sr_strdup("Invalid type of \"signal\"; integer expected.");
            sr_core_stacktrace_free(result);
            return NULL;
        }

        result->signal = root->u.object.values[i].value->u.integer;
        break;
    }

    /* Read executable. */
    for (unsigned i = 0; i < root->u.object.length; ++i)
    {
        if (0 != strcmp("executable", root->u.object.values[i].name))
            continue;

        if (root->u.object.values[i].value->type != SR_JSON_STRING)
        {
            *error_message = sr_strdup("Invalid type of \"executable\"; string expected.");
            sr_core_stacktrace_free(result);
            return NULL;
        }

        result->executable = sr_strdup(root->u.object.values[i].value->u.string.ptr);
        break;
    }

    /* Read threads. */
    for (unsigned i = 0; i < root->u.object.length; ++i)
    {
        if (0 != strcmp("threads", root->u.object.values[i].name))
            continue;

        if (root->u.object.values[i].value->type != SR_JSON_ARRAY)
        {
            *error_message = sr_strdup("Invalid type of \"threads\"; array expected.");
            sr_core_stacktrace_free(result);
            return NULL;
        }

        for (unsigned j = 0; j < root->u.object.values[i].value->u.array.length; ++j)
        {
            struct sr_core_thread *thread = sr_core_thread_from_json(
                root->u.object.values[i].value->u.array.values[j],
                error_message);

            if (!thread)
            {
                sr_core_stacktrace_free(result);
                return NULL;
            }

            result->threads = sr_core_thread_append(result->threads, thread);
        }

        break;
    }

    /* Read crash thread. */
    for (unsigned i = 0; i < root->u.object.length; ++i)
    {
        if (0 != strcmp("crash_thread", root->u.object.values[i].name))
            continue;

        if (root->u.object.values[i].value->type != SR_JSON_INTEGER)
        {
            *error_message = sr_strdup("Invalid type of \"crash_thread\"; integer expected.");
            sr_core_stacktrace_free(result);
            return NULL;
        }

        long crash_thread = root->u.object.values[i].value->u.integer;
        if (crash_thread < 0)
        {
            *error_message = sr_strdup("Invalid index in \"crash_thread\".");
            sr_core_stacktrace_free(result);
            return NULL;
        }

        result->crash_thread = result->threads;
        for (long j = 0; j < crash_thread; ++j)
        {
            if (!result->crash_thread)
            {
                *error_message = sr_strdup("Invalid index in \"crash_thread\".");
                sr_core_stacktrace_free(result);
                return NULL;
            }

            result->crash_thread = result->crash_thread->next;
        }

        if (!result->crash_thread)
        {
            *error_message = sr_strdup("Invalid index in \"crash_thread\".");
            sr_core_stacktrace_free(result);
            return NULL;
        }

        break;
    }

    return result;
}

struct sr_core_stacktrace *
sr_core_stacktrace_from_json_text(const char *text,
                                  char **error_message)
{
    struct sr_json_settings settings;
    memset(&settings, 0, sizeof(struct sr_json_settings));
    struct sr_location location;
    sr_location_init(&location);
    struct sr_json_value *json_root = sr_json_parse_ex(&settings,
                                                        text,
                                                        &location);

    if (!json_root)
    {
        *error_message = sr_location_to_string(&location);
        return NULL;
    }

    return sr_core_stacktrace_from_json(json_root,
                                        error_message);
}

char *
sr_core_stacktrace_to_json(struct sr_core_stacktrace *stacktrace)
{
    struct sr_strbuf *strbuf = sr_strbuf_new();
    sr_strbuf_append_strf(strbuf,
                          "{   \"signal\": %"PRIu8"\n",
                          stacktrace->signal);

    /* Crash thread offset. */
    if (stacktrace->crash_thread)
    {
        /* Calculate the offset of the crash thread. */
        uint32_t offset = 0;
        struct sr_core_thread *thread = stacktrace->threads;
        while (thread && thread != stacktrace->crash_thread)
        {
            thread = thread->next;
            ++offset;
        }

        assert(thread);
        sr_strbuf_append_strf(strbuf, ",   \"crash_thread\": %"PRIu32"\n", offset);
    }

    if (stacktrace->executable)
    {
        char *escaped_executable = sr_json_escape(stacktrace->executable);
        sr_strbuf_append_strf(strbuf, ",   \"executable\": \"%s\"\n", escaped_executable);
        free(escaped_executable);
    }

    sr_strbuf_append_str(strbuf, ",   \"threads\":\n");

    struct sr_core_thread *thread = stacktrace->threads;
    while (thread)
    {
        if (thread == stacktrace->threads)
            sr_strbuf_append_str(strbuf, "      [ ");
        else
            sr_strbuf_append_str(strbuf, "      , ");

        char *thread_json = sr_core_thread_to_json(thread);
        char *indented_thread_json = sr_indent_except_first_line(thread_json, 8);
        sr_strbuf_append_str(strbuf, indented_thread_json);
        free(indented_thread_json);
        free(thread_json);
        thread = thread->next;
        if (thread)
            sr_strbuf_append_str(strbuf, "\n");
    }

    sr_strbuf_append_str(strbuf, " ]\n");
    sr_strbuf_append_char(strbuf, '}');
    return sr_strbuf_free_nobuf(strbuf);
}

struct sr_core_stacktrace *
sr_core_stacktrace_create(const char *gdb_stacktrace_text,
                          const char *unstrip_text,
                          const char *executable_path)
{
    // Parse the GDB stacktrace.
    struct sr_location location;
    sr_location_init(&location);

    struct sr_gdb_stacktrace *gdb_stacktrace =
        sr_gdb_stacktrace_parse(&gdb_stacktrace_text, &location);

    if (!gdb_stacktrace)
    {
        if (sr_debug_parser)
        {
            fprintf(stderr, "Unable to parse stacktrace: %d:%d: %s\n",
                    location.line, location.column, location.message);
        }

        return NULL;
    }

    // Parse the unstrip output.
    struct sr_unstrip_entry *unstrip = sr_unstrip_parse(unstrip_text);
    if (!unstrip)
    {
        if (sr_debug_parser)
            fprintf(stderr, "Unable to parse unstrip output.");

        return NULL;
    }

    // Create the core stacktrace
    struct sr_core_stacktrace *core_stacktrace =
        sr_core_stacktrace_new();

    struct sr_gdb_thread *gdb_thread = gdb_stacktrace->threads;
    while (gdb_thread)
    {
        struct sr_core_thread *core_thread = sr_core_thread_new();

        struct sr_gdb_frame *gdb_frame = gdb_thread->frames;
        while (gdb_frame)
        {
            gdb_frame = gdb_frame->next;

            struct sr_core_frame *core_frame = sr_core_frame_new();
            core_frame->address = gdb_frame->address;

            struct sr_unstrip_entry *unstrip_entry =
                sr_unstrip_find_address(unstrip, gdb_frame->address);

            if (unstrip_entry)
            {
                core_frame->build_id = sr_strdup(unstrip_entry->build_id);
                core_frame->build_id_offset = core_frame->address - unstrip_entry->start;
                core_frame->file_name = sr_strdup(unstrip_entry->file_name);
            }

            if (gdb_frame->function_name &&
                0 != strcmp(gdb_frame->function_name, "??"))
            {
                core_frame->function_name =
                    sr_strdup(gdb_frame->function_name);
            }
        }

        core_stacktrace->threads =
            sr_core_thread_append(core_stacktrace->threads,
                                  core_thread);

        gdb_thread = gdb_thread->next;
    }

    return core_stacktrace;
}
