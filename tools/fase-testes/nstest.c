// Teste: HTML -> libhubbub -> libdom. Devolve o numero de filhos de <body> ou -1.
#include <string.h>
#include <stdbool.h>
#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

static void nsmsg(uint32_t severity, void *ctx, const char *msg, ...) { (void)severity; (void)ctx; (void)msg; }

int nstest_count_tag(const char *html, size_t html_len, const char *tagname);

int nstest_parse(void)
{
    static const char html[] =
        "<html><head><title>x</title></head><body><p>a</p><p>b</p><div>c</div></body></html>";
    return nstest_count_tag(html, sizeof html - 1, "p");
}

// Analisa HTML com hubbub+dom e devolve quantos elementos <tagname> existem (-1 se falhar).
int nstest_count_tag(const char *html, size_t html_len, const char *tagname)
{
    dom_hubbub_parser_params p;
    memset(&p, 0, sizeof p);
    p.enc = "UTF-8"; p.fix_enc = false; p.msg = nsmsg;
    dom_hubbub_parser *parser = NULL;
    dom_document *doc = NULL;
    if (dom_hubbub_parser_create(&p, &parser, &doc) != DOM_HUBBUB_OK) return -1;
    dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)html, html_len);
    dom_hubbub_parser_completed(parser);

    int result = -1;
    dom_nodelist *list = NULL;
    dom_string *tag = NULL;
    if (dom_string_create((const uint8_t *)tagname, strlen(tagname), &tag) == DOM_NO_ERR) {
        if (dom_document_get_elements_by_tag_name(doc, tag, &list) == DOM_NO_ERR && list) {
            uint32_t n = 0;
            dom_nodelist_get_length(list, &n);
            result = (int)n;
            dom_nodelist_unref(list);
        }
        dom_string_unref(tag);
    }
    dom_hubbub_parser_destroy(parser);
    dom_node_unref(doc);
    return result;
}
