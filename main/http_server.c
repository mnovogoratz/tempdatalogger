#include "http_server.h"
#include "datalog.h"
#include "config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http";
static httpd_handle_t s_server = NULL;

// ── Embedded dashboard HTML ───────────────────────────────────────────────────
// Stored in flash (read-only data section).  The %s placeholder is filled at
// request time with a live JSON payload from datalog_to_json().
static const char HTML_TEMPLATE[] =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>TC Datalogger</title>"
"<link rel='preconnect' href='https://fonts.googleapis.com'>"
"<link href='https://fonts.googleapis.com/css2?family=DM+Mono:wght@300;400;500&family=DM+Sans:wght@300;400;500&display=swap' rel='stylesheet'>"
"<style>"
":root{"
"--bg:#f7f6f2;--surface:#fff;--border:#e2e0d8;--text:#1a1917;--muted:#8a8880;"
"--c1:#d97706;--c2:#7c3aed;--c3:#0891b2;--c4:#16a34a;"
"}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:'DM Sans',sans-serif;background:var(--bg);color:var(--text);padding:32px 20px;min-height:100vh}"
".wrap{max-width:900px;margin:0 auto}"
"header{margin-bottom:36px}"
".tag{font-family:'DM Mono',monospace;font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin-bottom:8px}"
"h1{font-size:clamp(20px,4vw,28px);font-weight:300;letter-spacing:-.02em}"
"h1 span{font-weight:500}"
".meta{font-size:13px;color:var(--muted);margin-top:6px}"
".cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px;margin-bottom:32px}"
".card{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:16px 18px}"
".card-label{font-size:11px;letter-spacing:.1em;text-transform:uppercase;color:var(--muted);margin-bottom:8px}"
".card-val{font-family:'DM Mono',monospace;font-size:26px;font-weight:500;line-height:1}"
".card-val.fault{color:var(--muted);font-size:16px}"
".card-unit{font-size:12px;color:var(--muted);margin-top:4px}"
".section-label{font-family:'DM Mono',monospace;font-size:10px;letter-spacing:.14em;text-transform:uppercase;"
"color:var(--muted);padding-bottom:8px;border-bottom:1px solid var(--border);margin-bottom:16px}"
"/* Chart */"
".chart-wrap{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:20px;margin-bottom:32px}"
"canvas{width:100%!important}"
"/* Table */"
".tbl-wrap{background:var(--surface);border:1px solid var(--border);border-radius:10px;overflow:auto;margin-bottom:32px}"
"table{width:100%;border-collapse:collapse;font-size:13px}"
"thead th{font-family:'DM Mono',monospace;font-size:10px;letter-spacing:.1em;text-transform:uppercase;"
"color:var(--muted);padding:12px 16px;text-align:right;border-bottom:1px solid var(--border);white-space:nowrap}"
"thead th:first-child{text-align:left}"
"tbody tr{border-bottom:1px solid var(--border)}"
"tbody tr:last-child{border-bottom:none}"
"tbody tr:hover{background:#faf9f6}"
"td{padding:10px 16px;text-align:right;font-family:'DM Mono',monospace}"
"td:first-child{text-align:left;color:var(--muted);font-size:12px}"
"td.fault{color:var(--border)}"
".dl-btn{display:inline-flex;align-items:center;gap:8px;padding:9px 18px;"
"background:var(--text);color:#fff;border:none;border-radius:6px;font-family:'DM Sans',sans-serif;"
"font-size:13px;cursor:pointer;text-decoration:none;transition:opacity .15s}"
".dl-btn:hover{opacity:.8}"
".refresh{float:right;font-family:'DM Mono',monospace;font-size:11px;color:var(--muted);margin-top:4px}"
"</style>"
"</head>"
"<body>"
"<div class='wrap'>"
"<header>"
"<div class='tag'>Thermocouple Datalogger</div>"
"<h1>Temperature <span>Log</span></h1>"
"<p class='meta' id='meta'>Loading&hellip;</p>"
"</header>"
"<div class='cards'>"
"<div class='card'><div class='card-label' style='color:var(--c1)'>TC 1</div>"
"<div class='card-val' id='v0'>—</div><div class='card-unit'>°C</div></div>"
"<div class='card'><div class='card-label' style='color:var(--c2)'>TC 2</div>"
"<div class='card-val' id='v1'>—</div><div class='card-unit'>°C</div></div>"
"<div class='card'><div class='card-label' style='color:var(--c3)'>TC 3</div>"
"<div class='card-val' id='v2'>—</div><div class='card-unit'>°C</div></div>"
"<div class='card'><div class='card-label' style='color:var(--c4)'>TC 4</div>"
"<div class='card-val' id='v3'>—</div><div class='card-unit'>°C</div></div>"
"</div>"
"<div class='chart-wrap'>"
"<div class='section-label'>Temperature History</div>"
"<canvas id='chart' height='220'></canvas>"
"</div>"
"<div style='margin-bottom:12px'>"
"<div class='section-label' style='display:inline-block;border:none;padding:0;margin:0'>Readings Table</div>"
"<span class='refresh' id='refresh-ts'></span>"
"</div>"
"<div class='tbl-wrap'>"
"<table>"
"<thead><tr><th>Time (s)</th><th style='color:var(--c1)'>TC 1 °C</th>"
"<th style='color:var(--c2)'>TC 2 °C</th><th style='color:var(--c3)'>TC 3 °C</th>"
"<th style='color:var(--c4)'>TC 4 °C</th></tr></thead>"
"<tbody id='tbl'><tr><td colspan='5' style='text-align:center;color:var(--muted);padding:24px'>No data yet</td></tr></tbody>"
"</table>"
"</div>"
"<a class='dl-btn' href='/data.csv' download='tc_log.csv'>"
"&#8595; Download CSV</a>"
"</div>"
"<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js'></script>"
"<script>"
"const COLORS=['#d97706','#7c3aed','#0891b2','#16a34a'];"
"const LABELS=['TC 1','TC 2','TC 3','TC 4'];"
"let chart;"
"function fmt(v){return v===null?'—':v.toFixed(1);}"
"function buildChart(entries){"
"const labels=entries.map(e=>e.t+'s');"
"const datasets=LABELS.map((lbl,i)=>({"
"label:lbl,"
"data:entries.map(e=>e.c[i]),"
"borderColor:COLORS[i],"
"backgroundColor:COLORS[i]+'18',"
"borderWidth:1.5,"
"pointRadius:entries.length>60?0:2,"
"tension:0.3,"
"spanGaps:true"
"}));"
"if(chart){chart.destroy();}"
"chart=new Chart(document.getElementById('chart'),{"
"type:'line',"
"data:{labels,datasets},"
"options:{responsive:true,animation:{duration:400},"
"plugins:{legend:{labels:{font:{family:'DM Sans'},boxWidth:12}}},"
"scales:{"
"x:{ticks:{maxTicksLimit:12,font:{family:'DM Mono',size:10},color:'#8a8880'},grid:{color:'#e2e0d8'}},"
"y:{ticks:{font:{family:'DM Mono',size:10},color:'#8a8880'},grid:{color:'#e2e0d8'},"
"title:{display:true,text:'°C',font:{family:'DM Mono',size:10},color:'#8a8880'}}"
"}}});"
"}"
"function buildTable(entries){"
"const tb=document.getElementById('tbl');"
"if(!entries.length){tb.innerHTML='<tr><td colspan=5 style=\"text-align:center;color:var(--muted);padding:24px\">No data yet</td></tr>';return;}"
"tb.innerHTML=[...entries].reverse().map(e=>"
"'<tr><td>'+e.t+'</td>'"
"+e.c.map(v=>'<td class=\"'+(v===null?'fault':'')+'\">'+fmt(v)+'</td>').join('')"
"+'</tr>').join('');"
"}"
"async function refresh(){"
"try{"
"const r=await fetch('/data.json');"
"const d=await r.json();"
"const entries=d.entries||[];"
"document.getElementById('meta').textContent=entries.length+' reading'+(entries.length===1?'':'s')+' stored';"
"document.getElementById('refresh-ts').textContent='updated '+new Date().toLocaleTimeString();"
"// Latest reading cards"
"if(entries.length){"
"const last=entries[entries.length-1];"
"last.c.forEach((v,i)=>{"
"const el=document.getElementById('v'+i);"
"if(v===null){el.textContent='FAULT';el.className='card-val fault';}"
"else{el.textContent=v.toFixed(1);el.className='card-val';}"
"});"
"}"
"buildChart(entries);"
"buildTable(entries);"
"}catch(e){console.error(e);}"
"}"
"refresh();"
"setInterval(refresh,30000);" // auto-refresh every 30s
"</script>"
"</body></html>";

// ── Handlers ─────────────────────────────────────────────────────────────────

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_TEMPLATE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_json(httpd_req_t *req)
{
    // Allocate a generous buffer; LOG_MAX_ENTRIES * ~60 bytes each
    size_t buf_len = LOG_MAX_ENTRIES * 64 + 32;
    char *buf = malloc(buf_len);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    datalog_to_json(buf, buf_len);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

static esp_err_t handler_csv(httpd_req_t *req)
{
    size_t buf_len = LOG_MAX_ENTRIES * 40 + 32;
    char *buf = malloc(buf_len);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    datalog_to_csv(buf, buf_len);
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"tc_log.csv\"");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

// ── Start / Stop ─────────────────────────────────────────────────────────────

esp_err_t http_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size       = 8192;

    esp_err_t ret = httpd_start(&s_server, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t uri_root = { .uri = "/",          .method = HTTP_GET, .handler = handler_root };
    httpd_uri_t uri_json = { .uri = "/data.json",  .method = HTTP_GET, .handler = handler_json };
    httpd_uri_t uri_csv  = { .uri = "/data.csv",   .method = HTTP_GET, .handler = handler_csv  };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_json);
    httpd_register_uri_handler(s_server, &uri_csv);

    ESP_LOGI(TAG, "HTTP server running at http://192.168.4.1/");
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
