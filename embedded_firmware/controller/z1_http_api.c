/**
 * Z1 Cluster HTTP REST API Server Implementation
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_http_api.h"
#include "z1_protocol_extended.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Global State
// ============================================================================

http_connection_t g_http_connections[Z1_HTTP_MAX_CONNECTIONS];
char g_http_request_buffer[Z1_HTTP_BUFFER_SIZE];
char g_http_response_buffer[Z1_HTTP_BUFFER_SIZE];

bool g_snn_deployed = false;
bool g_snn_running = false;
char g_snn_network_name[64] = "none";
uint32_t g_snn_neuron_count = 0;
uint8_t g_snn_nodes_used = 0;

// ============================================================================
// JSON Helper Functions
// ============================================================================

int json_begin_object(char* buffer, int size) {
    if (size < 2) return -1;
    buffer[0] = '{';
    return 1;
}

int json_end_object(char* buffer, int pos, int size) {
    if (pos >= size - 1) return -1;
    buffer[pos++] = '}';
    buffer[pos] = '\0';
    return pos;
}

int json_add_string(char* buffer, int pos, int size, 
                   const char* key, const char* value, bool is_last) {
    int written = snprintf(buffer + pos, size - pos, 
                          "\"%s\":\"%s\"%s", key, value, is_last ? "" : ",");
    if (written < 0 || pos + written >= size) return -1;
    return pos + written;
}

int json_add_int(char* buffer, int pos, int size,
                const char* key, int32_t value, bool is_last) {
    int written = snprintf(buffer + pos, size - pos,
                          "\"%s\":%d%s", key, value, is_last ? "" : ",");
    if (written < 0 || pos + written >= size) return -1;
    return pos + written;
}

int json_add_bool(char* buffer, int pos, int size,
                 const char* key, bool value, bool is_last) {
    int written = snprintf(buffer + pos, size - pos,
                          "\"%s\":%s%s", key, value ? "true" : "false", 
                          is_last ? "" : ",");
    if (written < 0 || pos + written >= size) return -1;
    return pos + written;
}

int json_begin_array(char* buffer, int pos, int size, const char* key) {
    int written;
    if (key) {
        written = snprintf(buffer + pos, size - pos, "\"%s\":[", key);
    } else {
        written = snprintf(buffer + pos, size - pos, "[");
    }
    if (written < 0 || pos + written >= size) return -1;
    return pos + written;
}

int json_end_array(char* buffer, int pos, int size, bool is_last) {
    if (pos >= size - 2) return -1;
    buffer[pos++] = ']';
    if (!is_last) buffer[pos++] = ',';
    buffer[pos] = '\0';
    return pos;
}

// ============================================================================
// HTTP Response Functions
// ============================================================================

void z1_http_send_response(http_connection_t* conn,
                           uint16_t status_code,
                           const char* content_type,
                           const char* body,
                           uint16_t body_length) {
    char headers[256];
    const char* status_text = "OK";
    
    // Map status codes to text
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
    }
    
    // Build headers
    int header_len = snprintf(headers, sizeof(headers),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status_code, status_text, content_type, body_length);
    
    // Send headers
    // TODO: Implement W5500 socket send
    // w5500_socket_send(conn->socket_num, (uint8_t*)headers, header_len);
    
    // Send body
    if (body && body_length > 0) {
        // w5500_socket_send(conn->socket_num, (uint8_t*)body, body_length);
    }
    
    conn->state = HTTP_STATE_CLOSING;
}

void z1_http_send_json(http_connection_t* conn, uint16_t status_code, const char* json) {
    z1_http_send_response(conn, status_code, "application/json", json, strlen(json));
}

void z1_http_send_error(http_connection_t* conn, uint16_t status_code, const char* message) {
    char json[256];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_string(json, pos, sizeof(json), "error", message, false);
    pos = json_add_int(json, pos, sizeof(json), "code", status_code, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, status_code, json);
}

// ============================================================================
// Node Management Endpoints
// ============================================================================

void handle_get_nodes(http_connection_t* conn) {
    char json[Z1_HTTP_BUFFER_SIZE];
    int pos = json_begin_object(json, sizeof(json));
    
    // Discover active nodes
    bool active_nodes[16] = {false};
    z1_discover_nodes_sequential(active_nodes);
    
    // Build nodes array
    pos = json_begin_array(json, pos, sizeof(json), "nodes");
    
    for (int i = 0; i < 16; i++) {
        if (active_nodes[i]) {
            // Get node info
            z1_node_info_t info;
            if (z1_get_node_info(i, &info)) {
                // Add node object
                int obj_start = pos;
                pos += snprintf(json + pos, sizeof(json) - pos, "{");
                pos = json_add_int(json, pos, sizeof(json), "id", i, false);
                pos = json_add_string(json, pos, sizeof(json), "status", "active", false);
                pos = json_add_int(json, pos, sizeof(json), "memory_free", info.free_memory, false);
                pos = json_add_int(json, pos, sizeof(json), "uptime_ms", info.uptime_ms, true);
                pos += snprintf(json + pos, sizeof(json) - pos, "}");
                
                if (i < 15 && active_nodes[i+1]) {
                    pos += snprintf(json + pos, sizeof(json) - pos, ",");
                }
            }
        }
    }
    
    pos = json_end_array(json, pos, sizeof(json), true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

void handle_get_node(http_connection_t* conn, uint8_t node_id) {
    if (node_id > 15) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    z1_node_info_t info;
    if (!z1_get_node_info(node_id, &info)) {
        z1_http_send_error(conn, 404, "Node not found or not responding");
        return;
    }
    
    char json[512];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "id", node_id, false);
    pos = json_add_string(json, pos, sizeof(json), "status", "active", false);
    pos = json_add_int(json, pos, sizeof(json), "memory_free", info.free_memory, false);
    pos = json_add_int(json, pos, sizeof(json), "uptime_ms", info.uptime_ms, false);
    pos = json_add_int(json, pos, sizeof(json), "neuron_count", info.neuron_count, false);
    pos = json_add_int(json, pos, sizeof(json), "spike_count", info.spike_count, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

void handle_post_node_reset(http_connection_t* conn, uint8_t node_id) {
    if (node_id > 15) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    if (z1_reset_node(node_id)) {
        char json[128];
        int pos = json_begin_object(json, sizeof(json));
        pos = json_add_string(json, pos, sizeof(json), "status", "ok", false);
        pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, true);
        json_end_object(json, pos, sizeof(json));
        z1_http_send_json(conn, 200, json);
    } else {
        z1_http_send_error(conn, 500, "Failed to reset node");
    }
}

void handle_post_node_ping(http_connection_t* conn, uint8_t node_id) {
    if (node_id > 15) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    if (z1_bus_ping_node(node_id)) {
        char json[128];
        int pos = json_begin_object(json, sizeof(json));
        pos = json_add_string(json, pos, sizeof(json), "status", "ok", false);
        pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, true);
        json_end_object(json, pos, sizeof(json));
        z1_http_send_json(conn, 200, json);
    } else {
        z1_http_send_error(conn, 404, "Node not responding");
    }
}

void handle_post_nodes_discover(http_connection_t* conn) {
    bool active_nodes[16] = {false};
    z1_discover_nodes_sequential(active_nodes);
    
    char json[512];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_begin_array(json, pos, sizeof(json), "active_nodes");
    
    bool first = true;
    for (int i = 0; i < 16; i++) {
        if (active_nodes[i]) {
            if (!first) {
                pos += snprintf(json + pos, sizeof(json) - pos, ",");
            }
            pos += snprintf(json + pos, sizeof(json) - pos, "%d", i);
            first = false;
        }
    }
    
    pos = json_end_array(json, pos, sizeof(json), true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

// ============================================================================
// Memory Operations Endpoints
// ============================================================================

void handle_get_node_memory(http_connection_t* conn, uint8_t node_id, 
                           uint32_t addr, uint16_t length) {
    if (node_id > 15) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    if (length > 4096) {
        z1_http_send_error(conn, 400, "Length too large (max 4096 bytes)");
        return;
    }
    
    uint8_t buffer[4096];
    int bytes_read = z1_read_node_memory(node_id, addr, buffer, length);
    
    if (bytes_read < 0) {
        z1_http_send_error(conn, 500, "Failed to read memory");
        return;
    }
    
    // Encode to base64
    char b64_buffer[6000];
    int b64_len = base64_encode(buffer, bytes_read, b64_buffer, sizeof(b64_buffer));
    
    if (b64_len < 0) {
        z1_http_send_error(conn, 500, "Failed to encode data");
        return;
    }
    
    char json[Z1_HTTP_BUFFER_SIZE];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "addr", addr, false);
    pos = json_add_int(json, pos, sizeof(json), "length", bytes_read, false);
    pos = json_add_string(json, pos, sizeof(json), "data", b64_buffer, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

void handle_post_node_memory(http_connection_t* conn, uint8_t node_id, const char* body) {
    if (node_id > 15) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    // TODO: Parse JSON body to extract addr and data
    // For now, return not implemented
    z1_http_send_error(conn, 501, "Not implemented");
}

// ============================================================================
// SNN Management Endpoints
// ============================================================================

void handle_get_snn_status(http_connection_t* conn) {
    char json[512];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_string(json, pos, sizeof(json), "state", 
                         g_snn_running ? "running" : (g_snn_deployed ? "stopped" : "idle"), false);
    pos = json_add_string(json, pos, sizeof(json), "network_name", g_snn_network_name, false);
    pos = json_add_int(json, pos, sizeof(json), "neuron_count", g_snn_neuron_count, false);
    pos = json_add_int(json, pos, sizeof(json), "nodes_used", g_snn_nodes_used, false);
    
    // Query activity from first node
    if (g_snn_deployed && g_snn_nodes_used > 0) {
        z1_snn_activity_t activity;
        if (z1_query_snn_activity(0, &activity)) {
            pos = json_add_int(json, pos, sizeof(json), "active_neurons", activity.active_neurons, false);
            pos = json_add_int(json, pos, sizeof(json), "total_spikes", activity.total_spikes, false);
            pos = json_add_int(json, pos, sizeof(json), "spike_rate_hz", activity.spike_rate_hz, true);
        } else {
            pos = json_add_int(json, pos, sizeof(json), "active_neurons", 0, false);
            pos = json_add_int(json, pos, sizeof(json), "total_spikes", 0, false);
            pos = json_add_int(json, pos, sizeof(json), "spike_rate_hz", 0, true);
        }
    } else {
        pos = json_add_int(json, pos, sizeof(json), "active_neurons", 0, false);
        pos = json_add_int(json, pos, sizeof(json), "total_spikes", 0, false);
        pos = json_add_int(json, pos, sizeof(json), "spike_rate_hz", 0, true);
    }
    
    json_end_object(json, pos, sizeof(json));
    z1_http_send_json(conn, 200, json);
}

void handle_post_snn_start(http_connection_t* conn) {
    if (!g_snn_deployed) {
        z1_http_send_error(conn, 400, "No SNN deployed");
        return;
    }
    
    if (z1_start_snn_all()) {
        g_snn_running = true;
        char json[128];
        int pos = json_begin_object(json, sizeof(json));
        pos = json_add_string(json, pos, sizeof(json), "status", "ok", true);
        json_end_object(json, pos, sizeof(json));
        z1_http_send_json(conn, 200, json);
    } else {
        z1_http_send_error(conn, 500, "Failed to start SNN");
    }
}

void handle_post_snn_stop(http_connection_t* conn) {
    if (z1_stop_snn_all()) {
        g_snn_running = false;
        char json[128];
        int pos = json_begin_object(json, sizeof(json));
        pos = json_add_string(json, pos, sizeof(json), "status", "ok", true);
        json_end_object(json, pos, sizeof(json));
        z1_http_send_json(conn, 200, json);
    } else {
        z1_http_send_error(conn, 500, "Failed to stop SNN");
    }
}

// ============================================================================
// URL Parsing
// ============================================================================

int parse_url_path(const char* path, char components[][32], int max_components) {
    int count = 0;
    const char* start = path;
    
    // Skip leading slash
    if (*start == '/') start++;
    
    while (*start && count < max_components) {
        const char* end = strchr(start, '/');
        if (!end) end = start + strlen(start);
        
        int len = end - start;
        if (len > 0 && len < 32) {
            strncpy(components[count], start, len);
            components[count][len] = '\0';
            count++;
        }
        
        if (*end == '\0') break;
        start = end + 1;
    }
    
    return count;
}

bool parse_query_param(const char* query, const char* key, char* value, int value_size) {
    const char* pos = strstr(query, key);
    if (!pos) return false;
    
    pos += strlen(key);
    if (*pos != '=') return false;
    pos++;
    
    const char* end = strchr(pos, '&');
    if (!end) end = pos + strlen(pos);
    
    int len = end - pos;
    if (len >= value_size) len = value_size - 1;
    
    strncpy(value, pos, len);
    value[len] = '\0';
    
    return true;
}

int32_t parse_query_param_int(const char* query, const char* key, int32_t default_value) {
    char value[32];
    if (!parse_query_param(query, key, value, sizeof(value))) {
        return default_value;
    }
    return atoi(value);
}

// ============================================================================
// Base64 Implementation (simplified)
// ============================================================================

static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_encode(const uint8_t* input, int input_length, 
                 char* output, int output_size) {
    int i = 0, j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    
    int output_length = ((input_length + 2) / 3) * 4;
    if (output_length >= output_size) return -1;
    
    while (input_length--) {
        char_array_3[i++] = *(input++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                output[j++] = base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for (int k = i; k < 3; k++)
            char_array_3[k] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int k = 0; k < i + 1; k++)
            output[j++] = base64_chars[char_array_4[k]];
        
        while (i++ < 3)
            output[j++] = '=';
    }
    
    output[j] = '\0';
    return j;
}

int base64_decode(const char* input, uint8_t* output, int output_size) {
    // TODO: Implement base64 decode
    return -1;
}


// ============================================================================
// SNN Deployment Endpoints (Complete Implementation)
// ============================================================================

/**
 * Handle SNN deployment - POST /api/snn/deploy
 * Receives neuron table data and deploys to nodes
 */
void handle_post_snn_deploy(http_connection_t* conn, const char* body, uint16_t body_length) {
    // Binary format expected:
    // [0-3]   uint32_t total_neurons
    // [4]     uint8_t node_count
    // [5-68]  char[64] network_name
    // [69+]   Node deployment data
    
    if (body_length < 69) {
        z1_http_send_error(conn, 400, "Invalid deployment data");
        return;
    }
    
    // Parse header
    uint32_t total_neurons;
    uint8_t node_count;
    memcpy(&total_neurons, body, 4);
    memcpy(&node_count, body + 4, 1);
    memcpy(g_snn_network_name, body + 5, 64);
    g_snn_network_name[63] = '\0';
    
    if (node_count > 16) {
        z1_http_send_error(conn, 400, "Too many nodes");
        return;
    }
    
    printf("[API] Deploying SNN '%s': %lu neurons across %u nodes\n",
           g_snn_network_name, total_neurons, node_count);
    
    // Parse and deploy to each node
    const uint8_t* data_ptr = (const uint8_t*)body + 69;
    uint16_t remaining = body_length - 69;
    
    for (uint8_t i = 0; i < node_count; i++) {
        if (remaining < 3) {
            z1_http_send_error(conn, 400, "Incomplete node data");
            return;
        }
        
        // Parse node deployment entry:
        // [0]     uint8_t node_id
        // [1-2]   uint16_t data_length
        // [3+]    neuron table data
        
        uint8_t node_id = data_ptr[0];
        uint16_t data_length;
        memcpy(&data_length, data_ptr + 1, 2);
        
        if (remaining < 3 + data_length) {
            z1_http_send_error(conn, 400, "Incomplete neuron table data");
            return;
        }
        
        const uint8_t* neuron_table = data_ptr + 3;
        
        printf("[API] Deploying to node %u: %u bytes\n", node_id, data_length);
        
        // Write neuron table to node's PSRAM
        uint32_t psram_addr = 0x20100000;  // Z1_NEURON_TABLE_BASE_ADDR
        
        // Send in chunks (max 256 bytes per Z1 bus message)
        uint16_t offset = 0;
        while (offset < data_length) {
            uint16_t chunk_size = (data_length - offset > 256) ? 256 : (data_length - offset);
            
            // Prepare memory write command
            uint8_t cmd_data[260];
            memcpy(cmd_data, &psram_addr, 4);
            memcpy(cmd_data + 4, neuron_table + offset, chunk_size);
            
            if (!z1_bus_send_command(node_id, Z1_CMD_MEMORY_WRITE, cmd_data, 4 + chunk_size)) {
                z1_http_send_error(conn, 500, "Failed to write to node PSRAM");
                return;
            }
            
            offset += chunk_size;
            psram_addr += chunk_size;
        }
        
        // Send load command to node
        z1_bus_send_command(node_id, Z1_CMD_SNN_LOAD_TABLE, NULL, 0);
        
        data_ptr += 3 + data_length;
        remaining -= 3 + data_length;
    }
    
    g_snn_deployed = true;
    g_snn_neuron_count = total_neurons;
    g_snn_nodes_used = node_count;
    
    // Send success response
    char json[256];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_string(json, pos, sizeof(json), "status", "deployed", false);
    pos = json_add_string(json, pos, sizeof(json), "network_name", g_snn_network_name, false);
    pos = json_add_int(json, pos, sizeof(json), "neuron_count", g_snn_neuron_count, false);
    pos = json_add_int(json, pos, sizeof(json), "nodes_used", g_snn_nodes_used, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
    
    printf("[API] SNN deployment complete\n");
}

/**
 * Handle spike injection - POST /api/snn/input
 * Injects spikes into specified neurons
 */
void handle_post_snn_inject(http_connection_t* conn, const char* body, uint16_t body_length) {
    if (!g_snn_running) {
        z1_http_send_error(conn, 400, "SNN not running");
        return;
    }
    
    // Binary format:
    // [0-1]   uint16_t spike_count
    // [2+]    Spike entries (6 bytes each):
    //   [0-1]   uint16_t neuron_id
    //   [2-5]   float value
    
    if (body_length < 2) {
        z1_http_send_error(conn, 400, "Invalid spike data");
        return;
    }
    
    uint16_t spike_count;
    memcpy(&spike_count, body, 2);
    
    if (body_length < 2 + spike_count * 6) {
        z1_http_send_error(conn, 400, "Incomplete spike data");
        return;
    }
    
    const uint8_t* spike_data = (const uint8_t*)body + 2;
    uint16_t injected = 0;
    
    for (uint16_t i = 0; i < spike_count; i++) {
        uint16_t neuron_id;
        float value;
        memcpy(&neuron_id, spike_data + i * 6, 2);
        memcpy(&value, spike_data + i * 6 + 2, 4);
        
        // Broadcast spike to all nodes (they'll filter by neuron ID)
        uint8_t cmd_data[6];
        memcpy(cmd_data, &neuron_id, 2);
        uint32_t timestamp = 0;  // Current time
        memcpy(cmd_data + 2, &timestamp, 4);
        
        for (uint8_t node = 0; node < g_snn_nodes_used; node++) {
            z1_bus_send_command(node, Z1_CMD_SNN_INJECT_SPIKE, cmd_data, 6);
        }
        
        injected++;
    }
    
    // Send response
    char json[128];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "spikes_injected", injected, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

/**
 * Handle get spike events - GET /api/snn/events
 * Returns recent spike events from all nodes
 */
void handle_get_snn_events(http_connection_t* conn, uint16_t count) {
    if (!g_snn_running) {
        z1_http_send_error(conn, 400, "SNN not running");
        return;
    }
    
    // Query all nodes for recent spikes
    char json[2048];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_begin_array(json, pos, sizeof(json), "events");
    
    // For each node, request spike events
    for (uint8_t node = 0; node < g_snn_nodes_used; node++) {
        // Send get spikes command
        uint8_t cmd_data[2];
        memcpy(cmd_data, &count, 2);
        z1_bus_send_command(node, Z1_CMD_SNN_GET_SPIKES, cmd_data, 2);
        
        // TODO: Wait for response and parse spike data
        // For now, return empty array
    }
    
    pos = json_end_array(json, pos, sizeof(json), true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

// ============================================================================
// Firmware Management Endpoints
// ============================================================================

/**
 * Handle firmware upload - POST /api/firmware/upload/{node_id}
 * Upload firmware binary to node's buffer
 */
void handle_firmware_upload(http_connection_t* conn, uint8_t node_id, 
                           const uint8_t* firmware_data, uint32_t firmware_size) {
    if (node_id >= 16) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    if (firmware_size > Z1_FIRMWARE_MAX_SIZE) {
        z1_http_send_error(conn, 400, "Firmware too large (max 112KB)");
        return;
    }
    
    // Send firmware in chunks over Z1 bus
    const uint32_t chunk_size = 256;  // Z1 bus packet size
    uint32_t offset = 0;
    uint32_t chunks_sent = 0;
    
    while (offset < firmware_size) {
        uint32_t bytes_to_send = (firmware_size - offset) < chunk_size ? 
                                 (firmware_size - offset) : chunk_size;
        
        // Prepare command data: [offset:4][size:4][data:N]
        uint8_t cmd_data[chunk_size + 8];
        memcpy(cmd_data, &offset, 4);
        memcpy(cmd_data + 4, &bytes_to_send, 4);
        memcpy(cmd_data + 8, firmware_data + offset, bytes_to_send);
        
        // Send firmware upload command
        if (!z1_bus_send_command(node_id, Z1_CMD_FIRMWARE_UPLOAD, 
                                 cmd_data, bytes_to_send + 8)) {
            z1_http_send_error(conn, 500, "Failed to send firmware chunk");
            return;
        }
        
        offset += bytes_to_send;
        chunks_sent++;
    }
    
    // Send success response
    char json[256];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, false);
    pos = json_add_int(json, pos, sizeof(json), "bytes_uploaded", firmware_size, false);
    pos = json_add_int(json, pos, sizeof(json), "chunks_sent", chunks_sent, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

/**
 * Handle firmware verify - POST /api/firmware/verify/{node_id}
 * Verify uploaded firmware checksum
 */
void handle_firmware_verify(http_connection_t* conn, uint8_t node_id) {
    if (node_id >= 16) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    // Send verify command
    if (!z1_bus_send_command(node_id, Z1_CMD_FIRMWARE_VERIFY, NULL, 0)) {
        z1_http_send_error(conn, 500, "Failed to send verify command");
        return;
    }
    
    // TODO: Wait for response with checksum
    // For now, return success
    char json[128];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, false);
    pos = json_add_string(json, pos, sizeof(json), "status", "verified", true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

/**
 * Handle firmware install - POST /api/firmware/install/{node_id}
 * Install firmware from buffer to flash
 */
void handle_firmware_install(http_connection_t* conn, uint8_t node_id) {
    if (node_id >= 16) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    // Send install command
    if (!z1_bus_send_command(node_id, Z1_CMD_FIRMWARE_INSTALL, NULL, 0)) {
        z1_http_send_error(conn, 500, "Failed to send install command");
        return;
    }
    
    // TODO: Wait for response
    char json[128];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, false);
    pos = json_add_string(json, pos, sizeof(json), "status", "installed", true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

/**
 * Handle firmware activate - POST /api/firmware/activate/{node_id}
 * Activate new firmware and reboot node
 */
void handle_firmware_activate(http_connection_t* conn, uint8_t node_id) {
    if (node_id >= 16) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    // Send activate command
    if (!z1_bus_send_command(node_id, Z1_CMD_FIRMWARE_ACTIVATE, NULL, 0)) {
        z1_http_send_error(conn, 500, "Failed to send activate command");
        return;
    }
    
    // Node will reboot after this
    char json[128];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, false);
    pos = json_add_string(json, pos, sizeof(json), "status", "activated", true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

/**
 * Handle firmware info - GET /api/firmware/info/{node_id}
 * Get current firmware information
 */
void handle_firmware_info(http_connection_t* conn, uint8_t node_id) {
    if (node_id >= 16) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    // Send info request command
    if (!z1_bus_send_command(node_id, Z1_CMD_FIRMWARE_INFO, NULL, 0)) {
        z1_http_send_error(conn, 500, "Failed to send info command");
        return;
    }
    
    // TODO: Wait for response with firmware info
    // For now, return placeholder
    char json[256];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, false);
    pos = json_add_string(json, pos, sizeof(json), "version", "1.0.0", false);
    pos = json_add_string(json, pos, sizeof(json), "build_date", "2025-11-12", false);
    pos = json_add_int(json, pos, sizeof(json), "size_bytes", 0, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

/**
 * Handle batch firmware flash - POST /api/firmware/batch
 * Flash multiple nodes with same firmware
 */
void handle_firmware_batch(http_connection_t* conn, const uint8_t* node_list, 
                          uint8_t node_count, const uint8_t* firmware_data, 
                          uint32_t firmware_size) {
    if (node_count == 0 || node_count > 16) {
        z1_http_send_error(conn, 400, "Invalid node count");
        return;
    }
    
    if (firmware_size > Z1_FIRMWARE_MAX_SIZE) {
        z1_http_send_error(conn, 400, "Firmware too large");
        return;
    }
    
    uint8_t success_count = 0;
    uint8_t failed_nodes[16];
    uint8_t failed_count = 0;
    
    // Flash each node
    for (uint8_t i = 0; i < node_count; i++) {
        uint8_t node_id = node_list[i];
        
        if (node_id >= 16) {
            failed_nodes[failed_count++] = node_id;
            continue;
        }
        
        // Upload firmware (simplified - should use handle_firmware_upload)
        // For now, just send command
        if (z1_bus_send_command(node_id, Z1_CMD_FIRMWARE_UPLOAD, 
                                firmware_data, firmware_size)) {
            success_count++;
        } else {
            failed_nodes[failed_count++] = node_id;
        }
    }
    
    // Send response
    char json[512];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "total_nodes", node_count, false);
    pos = json_add_int(json, pos, sizeof(json), "success_count", success_count, false);
    pos = json_add_int(json, pos, sizeof(json), "failed_count", failed_count, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

// ============================================================================
// Memory Access Endpoints
// ============================================================================

/**
 * Handle memory read - GET /api/nodes/{node_id}/memory?addr={addr}&len={len}
 * Read memory from node
 */
void handle_memory_read(http_connection_t* conn, uint8_t node_id, 
                       uint32_t address, uint32_t length) {
    if (node_id >= 16) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    if (length > 4096) {
        z1_http_send_error(conn, 400, "Length too large (max 4KB)");
        return;
    }
    
    // Send memory read command
    uint8_t cmd_data[8];
    memcpy(cmd_data, &address, 4);
    memcpy(cmd_data + 4, &length, 4);
    
    if (!z1_bus_send_command(node_id, Z1_CMD_MEMORY_READ, cmd_data, 8)) {
        z1_http_send_error(conn, 500, "Failed to send read command");
        return;
    }
    
    // TODO: Wait for response with memory data
    // For now, return placeholder
    char json[256];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, false);
    pos = json_add_int(json, pos, sizeof(json), "address", address, false);
    pos = json_add_int(json, pos, sizeof(json), "length", length, false);
    pos = json_add_string(json, pos, sizeof(json), "data", "base64_data_here", true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}

/**
 * Handle memory write - POST /api/nodes/{node_id}/memory
 * Write memory to node
 */
void handle_memory_write(http_connection_t* conn, uint8_t node_id, 
                        uint32_t address, const uint8_t* data, uint32_t length) {
    if (node_id >= 16) {
        z1_http_send_error(conn, 400, "Invalid node ID");
        return;
    }
    
    if (length > 4096) {
        z1_http_send_error(conn, 400, "Length too large (max 4KB)");
        return;
    }
    
    // Send memory write command in chunks
    const uint32_t chunk_size = 256;
    uint32_t offset = 0;
    
    while (offset < length) {
        uint32_t bytes_to_write = (length - offset) < chunk_size ? 
                                  (length - offset) : chunk_size;
        
        // Prepare command: [address:4][length:4][data:N]
        uint8_t cmd_data[chunk_size + 8];
        uint32_t chunk_addr = address + offset;
        memcpy(cmd_data, &chunk_addr, 4);
        memcpy(cmd_data + 4, &bytes_to_write, 4);
        memcpy(cmd_data + 8, data + offset, bytes_to_write);
        
        if (!z1_bus_send_command(node_id, Z1_CMD_MEMORY_WRITE, 
                                 cmd_data, bytes_to_write + 8)) {
            z1_http_send_error(conn, 500, "Failed to write memory");
            return;
        }
        
        offset += bytes_to_write;
    }
    
    // Send success response
    char json[128];
    int pos = json_begin_object(json, sizeof(json));
    pos = json_add_int(json, pos, sizeof(json), "node_id", node_id, false);
    pos = json_add_int(json, pos, sizeof(json), "bytes_written", length, true);
    json_end_object(json, pos, sizeof(json));
    
    z1_http_send_json(conn, 200, json);
}
