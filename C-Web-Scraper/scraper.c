#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include "mongoose.h"

/* ---------- Dynamic Buffer (HTML & Extracted Text dono ke liye) ---------- */
struct Buffer
{
    char *data;
    size_t size;
};

int append_to_buffer(struct Buffer *buf, const char *str, size_t len)
{
    if (len == 0)
        return 1;

    char *ptr = realloc(buf->data, buf->size + len + 1);
    if (ptr == NULL)
        return 0;

    buf->data = ptr;
    memcpy(buf->data + buf->size, str, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
    return 1;
}

/* ---------- HTML element checks (Aapka original logic) ---------- */
int is_content_tag(const xmlChar *name)
{
    return xmlStrEqual(name, BAD_CAST "h1") ||
           xmlStrEqual(name, BAD_CAST "h2") ||
           xmlStrEqual(name, BAD_CAST "h3") ||
           xmlStrEqual(name, BAD_CAST "p") ||
           xmlStrEqual(name, BAD_CAST "li");
}

int is_ignored_tag(const xmlChar *name)
{
    return xmlStrEqual(name, BAD_CAST "script") ||
           xmlStrEqual(name, BAD_CAST "style") ||
           xmlStrEqual(name, BAD_CAST "noscript") ||
           xmlStrEqual(name, BAD_CAST "svg");
}

/* ---------- Extract readable text into memory buffer ---------- */
void extract_text_to_buffer(xmlNode *node, struct Buffer *out)
{
    for (; node != NULL; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE)
            continue;

        if (is_ignored_tag(node->name))
            continue;

        if (is_content_tag(node->name))
        {
            xmlChar *text = xmlNodeGetContent(node);
            if (text != NULL)
            {
                if (text[0] != '\0')
                {
                    append_to_buffer(out, (char *)text, strlen((char *)text));
                    append_to_buffer(out, "\n\n", 2);
                }
                xmlFree(text);
            }
            continue;
        }

        extract_text_to_buffer(node->children, out);
    }
}

/* ---------- Download callback ---------- */
static size_t WriteHTMLCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    if (size != 0 && nmemb > (size_t)-1 / size)
        return 0;

    size_t realsize = size * nmemb;
    struct Buffer *mem = userp;

    if (!append_to_buffer(mem, contents, realsize))
    {
        fprintf(stderr, "Not enough memory!\n");
        return 0;
    }

    return realsize;
}

/* ---------- Download webpage ---------- */
struct Buffer GetRequest(const char *url, long *out_status)
{
    struct Buffer response = {NULL, 0};
    *out_status = 0;

    CURL *curl_handle = curl_easy_init();
    if (curl_handle == NULL)
        return response;

    response.data = malloc(1);
    if (response.data == NULL)
    {
        curl_easy_cleanup(curl_handle);
        return response;
    }
    response.data[0] = '\0';

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteHTMLCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "DataFetch/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)(10 * 1024 * 1024));
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK)
    {
        fprintf(stderr, "GET request failed: %s\n", curl_easy_strerror(res));
        free(response.data);
        response.data = NULL;
        response.size = 0;
    }
    else
    {
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, out_status);
    }

    curl_easy_cleanup(curl_handle);
    return response;
}

/* ---------- Parse HTML and return clean text ---------- */
struct Buffer ParseHTMLToText(const struct Buffer *html)
{
    struct Buffer text_out = {NULL, 0};
    if (html == NULL || html->data == NULL || html->size > INT_MAX)
        return text_out;

    htmlDocPtr doc = htmlReadMemory(
        html->data,
        (int)html->size,
        "download_page.html",
        NULL,
        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);

    if (doc == NULL)
        return text_out;

    text_out.data = malloc(1);
    if (text_out.data != NULL)
    {
        text_out.data[0] = '\0';
        xmlNode *root = xmlDocGetRootElement(doc);
        if (root != NULL)
            extract_text_to_buffer(root, &text_out);
    }

    xmlFreeDoc(doc);
    return text_out;
}

/* ---------- HTTP Server Event Handler ---------- */
static void fn(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        /* API Route: /api/scrape?url=... */
        if (mg_match(hm->uri, mg_str("/api/scrape"), NULL))
        {
            char url[2048] = {0};
            mg_http_get_var(&hm->query, "url", url, sizeof(url));

            if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
            {
                mg_http_reply(c, 400, "Content-Type: text/plain\r\n", "Invalid URL! Must start with http:// or https://");
                return;
            }

            printf("Scraping URL: %s\n", url);
            long status_code = 0;
            struct Buffer html = GetRequest(url, &status_code);

            if (html.data == NULL || status_code < 200 || status_code >= 300)
            {
                free(html.data);
                mg_http_reply(c, 502, "Content-Type: text/plain\r\n", "Failed to fetch webpage (HTTP Status: %ld)", status_code);
                return;
            }

            struct Buffer clean_text = ParseHTMLToText(&html);
            free(html.data);

            if (clean_text.data == NULL || clean_text.size == 0)
            {
                free(clean_text.data);
                mg_http_reply(c, 200, "Content-Type: text/plain; charset=utf-8\r\n", "No readable text found in <h1-h3>, <p>, or <li> tags.");
                return;
            }

            mg_http_reply(c, 200, "Content-Type: text/plain; charset=utf-8\r\n", "%s", clean_text.data);
            free(clean_text.data);
        }
        else
        {
            /* Baaki sabhi requests par index.html serve karein */
            struct mg_http_serve_opts opts = {.root_dir = "."};
            mg_http_serve_dir(c, hm, &opts);
        }
    }
}

/* ---------- Main Server Loop ---------- */
int main(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    xmlInitParser();

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    const char *listen_addr = "http://127.0.0.1:8080";
    if (mg_http_listen(&mgr, listen_addr, fn, NULL) == NULL)
    {
        fprintf(stderr, "Failed to start server on %s\n", listen_addr);
        return 1;
    }

    printf("==================================================\n");
    printf("Web Scraper Server is running!\n");
    printf("Open your browser and go to: %s\n", listen_addr);
    printf("Press Ctrl+C in this window to stop.\n");
    printf("==================================================\n");

    for (;;)
    {
        mg_mgr_poll(&mgr, 1000);
    }

    mg_mgr_free(&mgr);
    curl_global_cleanup();
    xmlCleanupParser();
    return 0;
}