#include "ngx_conf_extend_module.h"

static void ngx_conf_flush_files(ngx_cycle_t *cycle);
static char * ngx_conf_recursive_include(ngx_conf_t *cf, ngx_command_t *cmd,
                                         void *conf);


static ngx_command_t  ngx_conf_commands[] = {

    { ngx_string("recursive_include"),
      NGX_ANY_CONF|NGX_CONF_TAKE1,
      ngx_conf_recursive_include,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_module_t  ngx_conf_extend_module = {
    NGX_MODULE_V1,
    NULL,                                  /* module context */
    ngx_conf_commands,                     /* module directives */
    NGX_CONF_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_conf_flush_files,                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_conf_parse_pattern_file(ngx_conf_t *cf, ngx_str_t *pattern)
{
    char        *rv;
    ngx_int_t    n;
    ngx_glob_t   gl;
    ngx_str_t    name, file;

    ngx_memzero(&gl, sizeof(ngx_glob_t));

    gl.pattern = pattern->data;
    gl.log = cf->log;
    gl.test = 1;

    if (ngx_open_glob(&gl) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_open_glob_n " \"%s\" failed", pattern->data);
        return NGX_CONF_ERROR;
    }

    rv = NGX_CONF_OK;

    for ( ;; ) {
        n = ngx_read_glob(&gl, &name);

        if (n != NGX_OK) {
            break;
        }

        file.len = name.len++;
        file.data = ngx_pstrdup(cf->pool, &name);

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        rv = ngx_conf_parse(cf, &file);

        if (rv != NGX_CONF_OK) {
            break;
        }
    }

    ngx_close_glob(&gl);

    return rv;
}

static ngx_int_t
ngx_conf_pattern_tree_dumb(ngx_tree_ctx_t *ctx, ngx_str_t *name) {
    return NGX_OK;
}

static ngx_int_t
ngx_conf_pattern_tree_handler(ngx_tree_ctx_t *ctx, ngx_str_t *name)
{
    u_char                  *p, *n;
    ngx_str_t                file;
    ngx_uint_t               len;
    ngx_conf_pattern_ctx_t  *pctx;

    pctx = (ngx_conf_pattern_ctx_t *)ctx->data;

    n = ngx_pnalloc(pctx->conf->pool, name->len + pctx->pattern.len + 2);
    if (n == NULL) {
        return NGX_ERROR;
    }

    len = name->len;
    p = ngx_cpymem(n, name->data, len);
    if (name->data[len - 1] != '/') {
        *p++ = '/';
        len++;
    }
    ngx_cpystrn(p, pctx->pattern.data, pctx->pattern.len + 1);

    file.data = n;
    file.len = len + pctx->pattern.len;

    if (ngx_conf_parse_pattern_file(pctx->conf, &file) != NGX_CONF_OK)
        return NGX_ERROR;

    return NGX_OK;
}


static char *
ngx_conf_parse_pattern_tree(ngx_conf_t *cf, ngx_str_t *pattern)
{
    u_char                  *p, *last;
    ngx_str_t                tree;
    ngx_tree_ctx_t           ctx;
    ngx_conf_pattern_ctx_t   pctx;


    p = pattern->data;
    last = p + pattern->len - 1;

    while (*last != '/' && last > p) {
        last--;
    }

    if (last > p) {
        tree.data = p;
        tree.len = last - p;
        tree.data[tree.len] = '\0';

        pattern->data = last + 1;
        pattern->len -= (tree.len + 1);
    } else {
        tree.data = p;
        tree.len = 1;

        pattern->data = p + 1;
        pattern->len -= 1;
    }

    pctx.conf = cf;
    pctx.pattern = *pattern;
    ngx_memzero(&ctx, sizeof(ngx_tree_ctx_t));

    ctx.data = &pctx;
    ctx.log = cf->log;
    ctx.file_handler = ngx_conf_pattern_tree_dumb;
    ctx.pre_tree_handler = ngx_conf_pattern_tree_handler;
    ctx.post_tree_handler = ngx_conf_pattern_tree_dumb;
    ctx.spec_handler = ngx_conf_pattern_tree_dumb;

    if (ngx_walk_tree(&ctx, &tree) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_conf_recursive_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t   *value, file;

    value = cf->args->elts;
    file = value[1];

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

    if (ngx_conf_full_name(cf->cycle, &file, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (strpbrk((char *) file.data, "*?[") == NULL) {

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        return ngx_conf_parse(cf, &file);
    }

    return ngx_conf_parse_pattern_tree(cf, &file);
}

static void
ngx_conf_flush_files(ngx_cycle_t *cycle)
{
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "flush files");

    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].flush) {
            file[i].flush(&file[i], cycle->log);
        }
    }
}
