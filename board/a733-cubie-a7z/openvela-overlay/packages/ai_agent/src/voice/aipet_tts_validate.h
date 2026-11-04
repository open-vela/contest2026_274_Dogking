/* SPDX-License-Identifier: Apache-2.0 */
/* Validate the entire official V3 NDJSON response before legacy decoding can
 * accept an incomplete prefix. Completion code 20000000 is mandatory.
 * Called with a bounded NUL-terminated response by volc_tts.c. */
static int aipet_tts_validate(char *response, unsigned char *pcm,
                             size_t capacity, size_t *bytes)
{
    size_t total = 0;
    int finished = 0;
    char *line = response;
    *bytes = 0;
    while (*line) {
        while (*line == '\r' || *line == '\n' || *line == ' ' || *line == '\t') line++;
        if (!*line) break;
        if (finished) return -EPROTO;
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        const char *end = NULL;
        cJSON *obj = cJSON_ParseWithOpts(line, &end, 1);
        if (eol) *eol = '\n';
        if (!obj) return -EPROTO;
        cJSON *code = cJSON_GetObjectItemCaseSensitive(obj, "code");
        if (!cJSON_IsNumber(code) || (code->valuedouble != 0 && code->valuedouble != 20000000)) {
            cJSON_Delete(obj); return -EIO;
        }
        cJSON *data = cJSON_GetObjectItemCaseSensitive(obj, "data");
        if (data && !cJSON_IsString(data) && !cJSON_IsNull(data)) { cJSON_Delete(obj); return -EPROTO; }
        if (cJSON_IsString(data) && data->valuestring[0]) {
            size_t decoded = 0;
            int ret = mbedtls_base64_decode(pcm + total, capacity - total,
                &decoded, (const unsigned char *)data->valuestring, strlen(data->valuestring));
            if (ret) { cJSON_Delete(obj); return ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL ? -EMSGSIZE : -EPROTO; }
            total += decoded;
        }
        finished = code->valuedouble == 20000000;
        cJSON_Delete(obj);
        line = eol ? eol + 1 : line + strlen(line);
    }
    if (!finished || !total || (total & 1)) return -EPROTO;
    *bytes = total;
    return 0;
}
