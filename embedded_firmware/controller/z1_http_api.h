/**
 * Z1 Cluster HTTP REST API Server
 * 
 * RESTful HTTP server for Z1 cluster management and SNN operations.
 * Runs on controller node, translates HTTP requests to Z1 bus commands.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_HTTP_API_H
#define Z1_HTTP_API_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Configuration
// ============================================================================

#define Z1_HTTP_PORT            80
#define Z1_HTTP_MAX_CONNECTIONS 4
#define Z1_HTTP_BUFFER_SIZE     2048
#define Z1_HTTP_TIMEOUT_MS      30000

// ============================================================================
// HTTP Server State
// ============================================================================

typedef enum {
    HTTP_STATE_IDLE = 0,
    HTTP_STATE_RECEIVING_HEADERS,
    HTTP_STATE_RECEIVING_BODY,
    HTTP_STATE_PROCESSING,
    HTTP_STATE_SENDING_RESPONSE,
    HTTP_STATE_CLOSING
} http_state_t;

typedef struct {
    uint8_t socket_num;
    http_state_t state;
    uint16_t content_length;
    uint16_t bytes_received;
    uint16_t bytes_sent;
    uint32_t last_activity_ms;
    char method[8];
    char path[128];
    char content_type[32];
} http_connection_t;

// ============================================================================
// API Endpoint Handlers
// ============================================================================

/**
 * Initialize HTTP API server
 * 
 * @return true if successful
 */
bool z1_http_api_init(void);

/**
 * Process HTTP server (call from main loop)
 */
void z1_http_api_process(void);

/**
 * Handle incoming HTTP request
 * 
 * @param conn Connection structure
 * @param request_buffer Request data
 * @param request_length Request length
 * @return true if request handled successfully
 */
bool z1_http_handle_request(http_connection_t* conn, 
                            const char* request_buffer,
                            uint16_t request_length);

// ============================================================================
// Endpoint Handlers
// ============================================================================

// Node Management Endpoints
void handle_get_nodes(http_connection_t* conn);
void handle_get_node(http_connection_t* conn, uint8_t node_id);
void handle_post_node_reset(http_connection_t* conn, uint8_t node_id);
void handle_post_node_ping(http_connection_t* conn, uint8_t node_id);
void handle_post_node_led(http_connection_t* conn, uint8_t node_id, const char* body);
void handle_post_nodes_discover(http_connection_t* conn);

// Memory Operations Endpoints
void handle_get_node_memory(http_connection_t* conn, uint8_t node_id, 
                           uint32_t addr, uint16_t length);
void handle_post_node_memory(http_connection_t* conn, uint8_t node_id, const char* body);
void handle_post_node_execute(http_connection_t* conn, uint8_t node_id, const char* body);

// SNN Management Endpoints
void handle_post_snn_deploy(http_connection_t* conn, const char* body);
void handle_get_snn_topology(http_connection_t* conn);
void handle_post_snn_weights(http_connection_t* conn, const char* body);
void handle_get_snn_activity(http_connection_t* conn, uint32_t duration_ms);
void handle_post_snn_input(http_connection_t* conn, const char* body);
void handle_post_snn_start(http_connection_t* conn);
void handle_post_snn_stop(http_connection_t* conn);
void handle_get_snn_status(http_connection_t* conn);

// ============================================================================
// HTTP Response Helpers
// ============================================================================

/**
 * Send HTTP response
 * 
 * @param conn Connection structure
 * @param status_code HTTP status code
 * @param content_type Content type
 * @param body Response body
 * @param body_length Body length
 */
void z1_http_send_response(http_connection_t* conn,
                           uint16_t status_code,
                           const char* content_type,
                           const char* body,
                           uint16_t body_length);

/**
 * Send JSON response
 * 
 * @param conn Connection structure
 * @param status_code HTTP status code
 * @param json JSON string
 */
void z1_http_send_json(http_connection_t* conn, uint16_t status_code, const char* json);

/**
 * Send error response
 * 
 * @param conn Connection structure
 * @param status_code HTTP status code
 * @param message Error message
 */
void z1_http_send_error(http_connection_t* conn, uint16_t status_code, const char* message);

/**
 * Send chunked response (for large data)
 * 
 * @param conn Connection structure
 * @param status_code HTTP status code
 * @param content_type Content type
 * @return true if headers sent successfully
 */
bool z1_http_begin_chunked(http_connection_t* conn, 
                           uint16_t status_code,
                           const char* content_type);

/**
 * Send chunk of data
 * 
 * @param conn Connection structure
 * @param data Chunk data
 * @param length Chunk length
 * @return true if successful
 */
bool z1_http_send_chunk(http_connection_t* conn, const char* data, uint16_t length);

/**
 * End chunked response
 * 
 * @param conn Connection structure
 */
void z1_http_end_chunked(http_connection_t* conn);

// ============================================================================
// JSON Helper Functions
// ============================================================================

/**
 * Begin JSON object
 * 
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Current position in buffer
 */
int json_begin_object(char* buffer, int size);

/**
 * End JSON object
 * 
 * @param buffer Output buffer
 * @param pos Current position
 * @param size Buffer size
 * @return New position
 */
int json_end_object(char* buffer, int pos, int size);

/**
 * Add string field to JSON
 * 
 * @param buffer Output buffer
 * @param pos Current position
 * @param size Buffer size
 * @param key Field key
 * @param value Field value
 * @param is_last true if this is the last field
 * @return New position
 */
int json_add_string(char* buffer, int pos, int size, 
                   const char* key, const char* value, bool is_last);

/**
 * Add integer field to JSON
 * 
 * @param buffer Output buffer
 * @param pos Current position
 * @param size Buffer size
 * @param key Field key
 * @param value Field value
 * @param is_last true if this is the last field
 * @return New position
 */
int json_add_int(char* buffer, int pos, int size,
                const char* key, int32_t value, bool is_last);

/**
 * Add boolean field to JSON
 * 
 * @param buffer Output buffer
 * @param pos Current position
 * @param size Buffer size
 * @param key Field key
 * @param value Field value
 * @param is_last true if this is the last field
 * @return New position
 */
int json_add_bool(char* buffer, int pos, int size,
                 const char* key, bool value, bool is_last);

/**
 * Begin JSON array
 * 
 * @param buffer Output buffer
 * @param pos Current position
 * @param size Buffer size
 * @param key Array key (NULL for anonymous array)
 * @return New position
 */
int json_begin_array(char* buffer, int pos, int size, const char* key);

/**
 * End JSON array
 * 
 * @param buffer Output buffer
 * @param pos Current position
 * @param size Buffer size
 * @param is_last true if this is the last field
 * @return New position
 */
int json_end_array(char* buffer, int pos, int size, bool is_last);

// ============================================================================
// URL Parsing
// ============================================================================

/**
 * Parse URL path and extract components
 * 
 * @param path URL path
 * @param components Array to receive path components
 * @param max_components Maximum number of components
 * @return Number of components parsed
 */
int parse_url_path(const char* path, char components[][32], int max_components);

/**
 * Parse query string parameters
 * 
 * @param query Query string
 * @param key Parameter key to find
 * @param value Buffer to receive value
 * @param value_size Buffer size
 * @return true if parameter found
 */
bool parse_query_param(const char* query, const char* key, char* value, int value_size);

/**
 * Parse integer query parameter
 * 
 * @param query Query string
 * @param key Parameter key
 * @param default_value Default value if not found
 * @return Parameter value or default
 */
int32_t parse_query_param_int(const char* query, const char* key, int32_t default_value);

// ============================================================================
// Base64 Encoding/Decoding
// ============================================================================

/**
 * Encode data to base64
 * 
 * @param input Input data
 * @param input_length Input length
 * @param output Output buffer
 * @param output_size Output buffer size
 * @return Number of bytes written, or -1 on error
 */
int base64_encode(const uint8_t* input, int input_length, 
                 char* output, int output_size);

/**
 * Decode base64 to data
 * 
 * @param input Base64 string
 * @param output Output buffer
 * @param output_size Output buffer size
 * @return Number of bytes written, or -1 on error
 */
int base64_decode(const char* input, uint8_t* output, int output_size);

// ============================================================================
// Global State
// ============================================================================

// Active HTTP connections
extern http_connection_t g_http_connections[Z1_HTTP_MAX_CONNECTIONS];

// Request/response buffers
extern char g_http_request_buffer[Z1_HTTP_BUFFER_SIZE];
extern char g_http_response_buffer[Z1_HTTP_BUFFER_SIZE];

// SNN state
extern bool g_snn_deployed;
extern bool g_snn_running;
extern char g_snn_network_name[64];
extern uint32_t g_snn_neuron_count;
extern uint8_t g_snn_nodes_used;

#endif // Z1_HTTP_API_H
