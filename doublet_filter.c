/*
 * Copyright (C) 2015  Stefan May <smay@4finger.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#define TOPIC_MAX 2048
#define PAYLOAD_MAX 2048

static pthread_mutex_t doublet_lock = PTHREAD_MUTEX_INITIALIZER;
static LIST_HEAD(doublet_list, doublet_entry) doublet_head =
        LIST_HEAD_INITIALIZER(doublet_head);
struct doublet_entry {
    char* topic;
    char* payload;
    LIST_ENTRY(doublet_entry) entries;
};

inline static void* emalloc(size_t size) {
    void* ptr = malloc(size);
    if ( ptr == NULL ) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

inline static void* emalloc_copy(void* src, size_t size)
{
    void* ptr = emalloc(size);
    return strncpy(ptr, src, size);
}

void doublet_add(char* topic, char* payload)
{
    pthread_mutex_lock(&doublet_lock);

    struct doublet_entry* element = emalloc(sizeof(struct doublet_entry));

    element->topic = emalloc_copy(topic, TOPIC_MAX);
    element->payload = emalloc_copy(payload, PAYLOAD_MAX);

    LIST_INSERT_HEAD(&doublet_head, element, entries);

    pthread_mutex_unlock(&doublet_lock);
}

bool doublet_detected(char* topic, char* payload)
{
    pthread_mutex_lock(&doublet_lock);

    bool found = false;
    struct doublet_entry* element;
    LIST_FOREACH(element, &doublet_head, entries) {
        if ( !strcmp(element->topic, topic) && !strcmp(element->payload, payload) ) {
            LIST_REMOVE(element, entries);

            free(element->topic);
            free(element->payload);
            free(element);

            found = true;
        }
    }

    pthread_mutex_unlock(&doublet_lock);
    return found;
}
