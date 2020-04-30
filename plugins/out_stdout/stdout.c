/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2020 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_slist.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_config_map.h>
#include <msgpack.h>

#include "stdout.h"

#define A_NEW_KEY        "key"
#define A_NEW_KEY_LEN    3
#define A_NEW_VALUE      "value"
#define A_NEW_VALUE_LEN  5

static int cb_stdout_init(struct flb_output_instance *ins,
                          struct flb_config *config, void *data)
{

       
    int ret;
    const char *tmp;
    struct flb_stdout *ctx = NULL;
    (void) ins;
    (void) config;
    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_stdout));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;

    ret = flb_output_config_map_set(ins, (void *) ctx);
    if (ret == -1) {
        flb_free(ctx);
        return -1;
    }

    ctx->out_format = FLB_PACK_JSON_FORMAT_NONE;
    tmp = flb_output_get_property("format", ins);
    if (tmp) {
        ret = flb_pack_to_json_format_type(tmp);
        if (ret == -1) {
            flb_plg_error(ctx->ins, "unrecognized 'format' option. "
                          "Using 'msgpack'");
        }
        else {
            ctx->out_format = ret;
        }
    }

    /* Date format for JSON output */
    ctx->json_date_format = FLB_PACK_JSON_DATE_DOUBLE;
    tmp = flb_output_get_property("json_date_format", ins);
    if (tmp) {
        ret = flb_pack_to_json_date_type(tmp);
        if (ret == -1) {
            flb_plg_error(ctx->ins, "invalid json_date_format '%s'. "
                          "Using 'double' type", tmp);
        }
        else {
            ctx->json_date_format = ret;
        }
    }

    tmp = flb_output_get_property("metric_namespace", ins);
    if (tmp) { 
        printf("[config] Metric Namespace=%s\n", tmp);
        ctx->metric_namespace = flb_sds_create(tmp);
    }

  
    tmp = flb_output_get_property("metric_dimensions", ins);
    if(tmp){
        printf("[config] Metric Dimensions=%s\n", tmp);
        ctx->metric_dimensions = flb_utils_split(tmp, ',', 256);
    }else{
        printf("[config] Metric Dimensions=NULL\n");
    }

    struct mk_list *head;
    struct flb_split_entry *entry;

    if (ctx->metric_dimensions) {
        mk_list_foreach(head, ctx->metric_dimensions) {
            entry = mk_list_entry(head, struct flb_split_entry, _head);
            printf("Dimension Key: %s\n", entry->value);
        }
    }else{
        printf("List not found\n");
    }

    /* Export context */
    flb_output_set_context(ins, ctx);

    return 0;
}

//Test
#define GAUGE 1
#define COUNTER 2
struct flb_metric{
    msgpack_object key;
    msgpack_object value;
    int metric_type;
    const char *metric_unit;
    struct flb_time timestamp;

    struct mk_list _head;
};

static void cb_stdout_flush(const void *data, size_t bytes,
                            const char *tag, int tag_len,
                            struct flb_input_instance *i_ins,
                            void *out_context,
                            struct flb_config *config)
{
    printf("Input plugin name: ------------------------------%s\n",i_ins->p->name);
    if(flb_sds_cmp(i_ins->p->name, "cpu", flb_sds_len(i_ins->p->name)) == 0) {
        printf("Match\n");
    }else{
        printf("Doesn't match\n");
    }
    if (strcmp(i_ins->p->name, "cpu")==0){
        printf("Match\n");
    }else {
        printf("Not a match\n");
    }


    msgpack_unpacked result;
    size_t off = 0, cnt = 0;
    struct flb_stdout *ctx = out_context;
    flb_sds_t json;
    char *buf = NULL;
    (void) i_ins;
    (void) config;
    struct flb_time tmp;
    msgpack_object *p;

    // Added for test
    int i = 0;
    int ret;
    struct flb_time tm;
    int total_records;
    int new_keys = 1;
    msgpack_sbuffer tmp_sbuf;
    msgpack_packer tmp_pck;
    msgpack_unpacked result2;
    msgpack_object  *obj;
    msgpack_object_kv *kv;

    struct flb_metric *metric;
    struct mk_list *metric_temp;
    struct mk_list *metric_head;
    struct mk_list flb_metrics;
    // Construct a list
    mk_list_init(&flb_metrics);

    /* Create temporary msgpack buffer */
    msgpack_sbuffer_init(&tmp_sbuf);
    msgpack_packer_init(&tmp_pck, &tmp_sbuf, msgpack_sbuffer_write);

    /* Iterate over each item */
    msgpack_unpacked_init(&result2);
    while (msgpack_unpack_next(&result2, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS) {
        /*
         * Each record is a msgpack array [timestamp, map] of the
         * timestamp and record map. We 'unpack' each record, and then re-pack
         * it with the new fields added.
         */

        if (result2.data.type != MSGPACK_OBJECT_ARRAY) {
            continue;
        }

        /* unpack the array of [timestamp, map] */
        flb_time_pop_from_msgpack(&tm, &result2, &obj);

        /* obj should now be the record map */
        if (obj->type != MSGPACK_OBJECT_MAP) {
            continue;
        }


        /* re-pack the array into a new buffer */
        msgpack_pack_array(&tmp_pck, 2);
        flb_time_append_to_msgpack(&tm, &tmp_pck, 0);

        /* new record map size is old size + the new keys we will add */
        total_records = obj->via.map.size + new_keys;
        msgpack_pack_map(&tmp_pck, total_records);

        /* iterate through the old record map and add it to the new buffer */
        kv = obj->via.map.ptr;
        for(i=0; i < obj->via.map.size; i++) {
            // Test add
            msgpack_object_print(stdout, (kv+i)->key);
            printf("\n");
            msgpack_object_print(stdout, (kv+i)->val);
            printf("\n");
            
            /*struct flb_metric temp_metric = {
                .key = (kv+i)->key,
                .value = (kv+i)->val,
                .metric_type = GAUGE,
                .timestamp = tm
            };*/
            metric = flb_malloc(sizeof(struct flb_metric));
            metric->key = (kv+i)->key;
            metric->value = (kv+i)->val;
            metric->metric_type = GAUGE;
            metric->metric_unit = "Percent";
            metric->timestamp = tm;
            
            mk_list_add(&metric->_head, &flb_metrics);

            //msgpack_pack_object(&tmp_pck, (kv+i)->key);
            //msgpack_pack_object(&tmp_pck, (kv+i)->val);
        }

        printf("list size...............%d\n", mk_list_size(&flb_metrics));
        /* Iterate through the list */
        flb_info("\nIterating through flb_metric list.................\n");
        mk_list_foreach_safe(metric_head, metric_temp, &flb_metrics) {
            struct flb_metric *an_item = mk_list_entry(metric_head, struct flb_metric, _head);
            printf("{timestamp: ");
            printf("%ld", an_item->timestamp.tm.tv_sec); 
            printf("; key: ");
            msgpack_object_print(stdout, an_item->key);
            printf("; value: ");
            msgpack_object_print(stdout, an_item->value);
            printf("; metric_type: ");
            if(an_item->metric_type == 1){
                printf("gauge");
            }else if(an_item->metric_type == 2){
                printf("counter");
            }
            printf("; metric_unit: ");
            printf("%s", an_item->metric_unit);
            printf("}\n");
        }

        /* msgpack::sbuffer is a simple buffer implementation. */
        msgpack_sbuffer sbuf_emf;
        msgpack_sbuffer_init(&sbuf_emf);

        
        /* serialize values into the buffer using msgpack_sbuffer_write callback function. */
        msgpack_packer packer_emf;
        msgpack_packer_init(&packer_emf, &sbuf_emf, msgpack_sbuffer_write);

        msgpack_pack_map(&packer_emf, 7);
        
        int m;
        for(m = 0; m < 2; m++){
        printf("Inside packer--------------- 1\n");
        msgpack_pack_str(&packer_emf, A_NEW_KEY_LEN);
        msgpack_pack_str_body(&packer_emf, A_NEW_KEY, A_NEW_KEY_LEN);
        msgpack_pack_str(&packer_emf, A_NEW_VALUE_LEN);
        msgpack_pack_str_body(&packer_emf, A_NEW_VALUE, A_NEW_VALUE_LEN);
        }
    
        msgpack_pack_str(&packer_emf, 7); 
        msgpack_pack_str_body(&packer_emf, "new_map", 7);
        msgpack_pack_map(&packer_emf, 2);
        for(m = 0; m < 2; m++){
            printf("Inside packer-------------2\n");
            printf(" buffer size: %ld\n", sbuf_emf.size);
            msgpack_pack_str(&packer_emf, A_NEW_KEY_LEN); 
            msgpack_pack_str_body(&packer_emf, A_NEW_KEY, A_NEW_KEY_LEN);
            msgpack_pack_str(&packer_emf, A_NEW_VALUE_LEN); 
            msgpack_pack_str_body(&packer_emf, A_NEW_VALUE, A_NEW_VALUE_LEN);
        }
        printf("Buffer size: %ld\n", sbuf_emf.size);
        /* deserialize the buffer into msgpack_object instance. */
        /* deserialized object is valid during the msgpack_zone instance alive. */
        msgpack_zone mempool;
        msgpack_zone_init(&mempool, 2048);

        msgpack_object deserialized;
        msgpack_unpack(sbuf_emf.data, sbuf_emf.size, NULL, &mempool, &deserialized);


        /* print the deserialized object. */
        msgpack_object_print(stdout, deserialized);
        puts("");


        /* append new keys */
        //msgpack_pack_str(&tmp_pck, A_NEW_KEY_LEN);
        //msgpack_pack_str_body(&tmp_pck, A_NEW_KEY, A_NEW_KEY_LEN);
        // msgpack_pack_str(&tmp_pck, A_NEW_VALUE_LEN);
        //msgpack_pack_str_body(&tmp_pck, A_NEW_VALUE, A_NEW_VALUE_LEN);

    }
    msgpack_unpacked_destroy(&result2);

    if (ctx->out_format != FLB_PACK_JSON_FORMAT_NONE) {
        json = flb_pack_msgpack_to_json_format(data, bytes,
                                               ctx->out_format,
                                               ctx->json_date_format,
                                               ctx->json_date_key);
        write(STDOUT_FILENO, json, flb_sds_len(json));
        flb_sds_destroy(json);

        /*
         * If we are 'not' in json_lines mode, we need to add an extra
         * breakline.
         */
        if (ctx->out_format != FLB_PACK_JSON_FORMAT_LINES) {
            printf("\n");
        }
        printf("\nCalling flush if block\n");
        fflush(stdout);
    }
    else {
        /* A tag might not contain a NULL byte */
        buf = flb_malloc(tag_len + 1);
        if (!buf) {
            flb_errno();
            FLB_OUTPUT_RETURN(FLB_RETRY);
        }
        memcpy(buf, tag, tag_len);
        buf[tag_len] = '\0';
        msgpack_unpacked_init(&result);
        while (msgpack_unpack_next(&result, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS) {
            printf("[%zd] %s: [", cnt++, buf);
            flb_time_pop_from_msgpack(&tmp, &result, &p);
            printf("%"PRIu32".%09lu, ", (uint32_t)tmp.tm.tv_sec, tmp.tm.tv_nsec);
            msgpack_object_print(stdout, *p);
            printf("]\n");
        }
        msgpack_unpacked_destroy(&result);
        flb_free(buf);
    }
    printf("\nCalling flush out of condition\n");
    fflush(stdout);

    printf("\nOne flush Call finished...................................\n");

    FLB_OUTPUT_RETURN(FLB_OK);
}

static int cb_stdout_exit(void *data, struct flb_config *config)
{
    struct flb_stdout *ctx = data;

    if (!ctx) {
        return 0;
    }

    flb_free(ctx);
    return 0;
}

/* Configuration properties map */
static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "format", NULL,
     0, FLB_FALSE, 0,
     NULL
    },
    {
     FLB_CONFIG_MAP_STR, "json_date_format", NULL,
     0, FLB_FALSE, 0,
     NULL
    },
    {
     FLB_CONFIG_MAP_STR, "json_date_key", "date",
     0, FLB_TRUE, offsetof(struct flb_stdout, json_date_key),
     NULL
    },
    {
     FLB_CONFIG_MAP_STR, "metric_namespace", NULL,
     0, FLB_FALSE, 0,
     NULL
    },
    {
     FLB_CONFIG_MAP_CLIST, "metric_dimensions", "",
     0, FLB_TRUE, offsetof(struct flb_stdout, metric_dimensions),
     "Dimensions (optional)"
    },

    /* EOF */
    {0}
};

/* Plugin registration */
struct flb_output_plugin out_stdout_plugin = {
    .name         = "stdout",
    .description  = "Prints events to STDOUT",
    .cb_init      = cb_stdout_init,
    .cb_flush     = cb_stdout_flush,
    .cb_exit      = cb_stdout_exit,
    .flags        = 0,
    .config_map   = config_map
};
