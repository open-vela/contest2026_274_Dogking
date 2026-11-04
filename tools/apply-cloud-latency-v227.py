#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build-time mechanical SDK staging; existing SDK snapshot restores this path."""
from pathlib import Path
import sys

HELPER = '''/* cloud-latency-v227: product chat, official DeepSeek flash only. */
#ifdef CONFIG_A733_PRODUCT_V1
static void desktop_cloud_latency(cJSON *body, const char *host, const char *model)
{
    if (strcmp(host, "api.deepseek.com") != 0 || strcmp(model, "deepseek-flash") != 0)
        return;
    cJSON *thinking = cJSON_CreateObject();
    if (!thinking) return;
    cJSON_AddStringToObject(thinking, "type", "disabled");
    cJSON_AddItemToObject(body, "thinking", thinking);
    syslog(LOG_INFO, "cloud-llm-v227: DeepSeek flash thinking disabled; no credentials logged\\n");
}
#endif

'''

def transform(source):
    assert 'cloud-latency-v227:' not in source
    anchor='int llm_chat(const char* system_prompt, const char* messages_json,'
    assert source.count(anchor)==1
    source=source.replace(anchor,HELPER+anchor,1)
    anchor='        model_name_for_api(model, llm_host));'
    assert source.count(anchor)==2
    return source.replace(anchor,anchor+'''\n#ifdef CONFIG_A733_PRODUCT_V1
    desktop_cloud_latency(body, llm_host, model);
#endif''')

if __name__=='__main__':
    p=Path(sys.argv[1]).resolve(strict=True)
    assert p.name=='llm_proxy.c' and p.parent.name=='llm'
    before=p.read_text();after=transform(before)
    # This is a staged mechanical rewrite under build-a733-wapi.sh's snapshot,
    # not an edit to the official SDK baseline or a user-config migration.
    p.write_text(after)
