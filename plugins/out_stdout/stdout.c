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

// Fluent Bit intermediate representation metric
#define GAUGE 1
#define COUNTER 2
#define PERCENT "Percent"
#define BYTES "Bytes"
struct flb_ir_metric{
    msgpack_object key;
    msgpack_object value;
    int metric_type;
    const char *metric_unit;
    struct flb_time timestamp;

    struct mk_list _head;
};

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

static void cb_stdout_flush(const void *data, size_t bytes,
                            const char *tag, int tag_len,
                            struct flb_input_instance *i_ins,
                            void *out_context,
                            struct flb_config *config)
{
    int ir_metric_type;
    char *ir_metric_unit;
    struct flb_time timestamp;
    if (strcmp(i_ins->p->name, "cpu")==0){
        ir_metric_type = GAUGE;
        ir_metric_unit = PERCENT;
    }else if(strcmp(i_ins->p->name, "mem")==0){
        ir_metric_type = GAUGE;
        ir_metric_unit = BYTES;
    }else{
        printf("Incompatible metric input.\n");
        return;
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
    // msgpack_unpacked result2;
    msgpack_object  *obj;
    msgpack_object_kv *kv;

    struct flb_ir_metric *metric;
    struct mk_list *metric_temp;
    struct mk_list *metric_head;
    

    /* Create temporary msgpack buffer */
    msgpack_sbuffer_init(&tmp_sbuf);
    msgpack_packer_init(&tmp_pck, &tmp_sbuf, msgpack_sbuffer_write);

    /* Iterate over each item */
    msgpack_unpacked_init(&result);
    while (msgpack_unpack_next(&result, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS) {
        /*
         * Each record is a msgpack array [timestamp, map] of the
         * timestamp and record map. We 'unpack' each record, and then re-pack
         * it with the new fields added.
         */

        if (result.data.type != MSGPACK_OBJECT_ARRAY) {
            continue;
        }

        /* unpack the array of [timestamp, map] */
        flb_time_pop_from_msgpack(&tm, &result, &obj);

        /* obj should now be the record map */
        if (obj->type != MSGPACK_OBJECT_MAP) {
            continue;
        }

        struct mk_list flb_ir_metrics;
        // Construct a list
        mk_list_init(&flb_ir_metrics);
        //printf("flb-ir-metric size: %d\n", mk_list_size(&flb_ir_metrics));

        /* iterate through the old record map and add it to the new buffer */
        kv = obj->via.map.ptr;
        for(i=0; i < obj->via.map.size; i++) {
            // Print key value pair
            // msgpack_object_print(stdout, (kv+i)->key);
            // printf("\n");
            // msgpack_object_print(stdout, (kv+i)->val);
            // printf("\n");
            
            metric = flb_malloc(sizeof(struct flb_ir_metric));
            metric->key = (kv+i)->key;
            metric->value = (kv+i)->val;
            metric->metric_type = ir_metric_type;
            metric->metric_unit = ir_metric_unit;
            metric->timestamp = tm;
            
            mk_list_add(&metric->_head, &flb_ir_metrics);
        }

         //printf("flb-ir-metric size: %d\n", mk_list_size(&flb_ir_metrics));

        /* Iterate through the flb-ir-metric list */
        /*flb_info("\nIterating through flb_ir_metric list.................\n");
        mk_list_foreach_safe(metric_head, metric_temp, &flb_ir_metrics) {
            struct flb_ir_metric *an_item = mk_list_entry(metric_head, struct flb_ir_metric, _head);
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
        }*/

        /* msgpack::sbuffer is a simple buffer implementation. */
        msgpack_sbuffer sbuf_emf;
        msgpack_sbuffer_init(&sbuf_emf);
        
        /* serialize values into the buffer using msgpack_sbuffer_write callback function. */
        msgpack_packer packer_emf;
        msgpack_packer_init(&packer_emf, &sbuf_emf, msgpack_sbuffer_write);

        msgpack_pack_map(&packer_emf, (mk_list_size(&flb_ir_metrics)) + 1);
        
        //Pack the aws map
        msgpack_pack_str(&packer_emf, 4);
        msgpack_pack_str_body(&packer_emf, "_aws", 4);

        msgpack_pack_map(&packer_emf, 2);

        msgpack_pack_str(&packer_emf, 9);
        msgpack_pack_str_body(&packer_emf, "Timestamp", 9);
        msgpack_pack_long_long(&packer_emf, tm.tm.tv_sec * 1000L);

        msgpack_pack_str(&packer_emf, 17);
        msgpack_pack_str_body(&packer_emf, "CloudWatchMetrics", 17);
        msgpack_pack_array(&packer_emf, 1);

        msgpack_pack_map(&packer_emf, 3);

        msgpack_pack_str(&packer_emf, 9);
        msgpack_pack_str_body(&packer_emf, "Namespace", 9);
        if(ctx->metric_namespace){
            msgpack_pack_str(&packer_emf, flb_sds_len(ctx->metric_namespace));
            msgpack_pack_str_body(&packer_emf, ctx->metric_namespace, flb_sds_len(ctx->metric_namespace));
        }else{
            msgpack_pack_str(&packer_emf, 18);
            msgpack_pack_str_body(&packer_emf, "fluent-bit-metrics", 18);
        }
        

        msgpack_pack_str(&packer_emf, 10);
        msgpack_pack_str_body(&packer_emf, "Dimensions", 10);
        msgpack_pack_str(&packer_emf, 5);
        msgpack_pack_str_body(&packer_emf, "Value", 5);

        msgpack_pack_str(&packer_emf, 7);
        msgpack_pack_str_body(&packer_emf, "Metrics", 7);
        msgpack_pack_array(&packer_emf, mk_list_size(&flb_ir_metrics));
        mk_list_foreach_safe(metric_head, metric_temp, &flb_ir_metrics) {
            struct flb_ir_metric *an_item = mk_list_entry(metric_head, struct flb_ir_metric, _head);
            msgpack_pack_map(&packer_emf, 2);
            msgpack_pack_str(&packer_emf, 4);
            msgpack_pack_str_body(&packer_emf, "Name", 4);
            msgpack_pack_object(&packer_emf, an_item->key);
            msgpack_pack_str(&packer_emf, 4);
            msgpack_pack_str_body(&packer_emf, "Unit", 4);
            msgpack_pack_str(&packer_emf, strlen(an_item->metric_unit));
            msgpack_pack_str_body(&packer_emf, an_item->metric_unit, strlen(an_item->metric_unit));
        }
        

        
        // Pack the metric vlaues for each record
        mk_list_foreach_safe(metric_head, metric_temp, &flb_ir_metrics) {
            struct flb_ir_metric *an_item = mk_list_entry(metric_head, struct flb_ir_metric, _head);
            msgpack_pack_object(&packer_emf, an_item->key);
            msgpack_pack_object(&packer_emf, an_item->value);
        }

        /*int m;
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
        */
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

    }
    msgpack_unpacked_destroy(&result);

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
    fflush(stdout);

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
