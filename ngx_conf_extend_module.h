#include <ngx_conf_file.h>

typedef struct {
    ngx_str_t                pattern;
    ngx_conf_t               *conf;
} ngx_conf_pattern_ctx_t;
