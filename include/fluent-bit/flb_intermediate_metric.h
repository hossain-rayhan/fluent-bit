// Fluent Bit intermediate metric representation 
#include <fluent-bit/flb_time.h>
#include <monkey/mk_core.h>
#include <msgpack.h>

// Metric Type- Gague or Counter
#define GAUGE 1
#define COUNTER 2

// Metric Unit
#define PERCENT "Percent"
#define BYTES "Bytes"

struct flb_ir_metric
{
    msgpack_object key;
    msgpack_object value;
    int metric_type;
    const char *metric_unit;
    struct flb_time timestamp;

    struct mk_list _head;
};