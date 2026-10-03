#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <time.h>
#include <mosquitto.h>
#include <sqlite3.h> // Added for Part 4 Blackbox

#define HTTP_PORT 8080
#define HTTPS_PORT 8443
#define STUDENT_ID "401101932"

// ==========================================
// USER CONFIGURATION (CHANGE THESE)
// ==========================================
#define LAPTOP_HOSTNAME "10.55.228.45"

// --- Global State ---
int current_persons = 0;
time_t last_email_time = 0;
struct mosquitto *mosq = NULL;

// Part 4 Global States
int guard_mode = 0;
int thermal_throttle = 0;
time_t last_frame_time = 0; // Left at 0 so Watchdog doesn't trip on boot
time_t last_watchdog_email = 0;
sqlite3 *db;

char mqtt_user[64] = {0};
char mqtt_pass[64] = {0};
char alert_email[128] = {0};

typedef struct {
    char timestamp[32];
    int count;
} DetectionRecord;

DetectionRecord history[5];
int history_index = 0;
int history_count = 0;

// --- Secrets Loading ---
void load_secrets() {
    strcpy(mqtt_user, "guard_user");
    strcpy(mqtt_pass, "secure_password123");
    strcpy(alert_email, "your_email@gmail.com");

    FILE *file = fopen("/home/guard/secrets.txt", "r");
    if (!file) return;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        char *carriage = strchr(line, '\r');
        if (carriage) *carriage = '\0';

        if (strncmp(line, "MQTT_USER=", 10) == 0) strcpy(mqtt_user, line + 10);
        else if (strncmp(line, "MQTT_PASS=", 10) == 0) strcpy(mqtt_pass, line + 10);
        else if (strncmp(line, "ALERT_EMAIL=", 12) == 0) strcpy(alert_email, line + 12);
    }
    fclose(file);
}

// --- SQLite Init (Part 4) ---
void init_db() {
    if (sqlite3_open("history.db", &db) != SQLITE_OK) {
        printf("[DB] Failed to open SQLite DB.\n");
        return;
    }
    const char *sql = "CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT, count INTEGER);";
    sqlite3_exec(db, sql, 0, 0, 0);
}

void add_history_record(int count) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", t);
    
    // Array History (Backward Compatibility for Part 2)
    strcpy(history[history_index].timestamp, time_buf);
    history[history_index].count = count;
    history_index = (history_index + 1) % 5;
    if (history_count < 5) history_count++;

    // SQLite History (Part 4 Blackbox)
    char sql[256];
    snprintf(sql, sizeof(sql), "INSERT INTO history (timestamp, count) VALUES ('%s', %d);", time_buf, count);
    sqlite3_exec(db, sql, 0, 0, 0);

    // Circular Buffer logic: delete old records if > 100
    sqlite3_exec(db, "DELETE FROM history WHERE id NOT IN (SELECT id FROM history ORDER BY id DESC LIMIT 100);", 0, 0, 0);
}

// --- Telemetry Functions ---
float get_cpu_temp() {
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return -1.0;
    int t;
    fscanf(f, "%d", &t);
    fclose(f);
    return t / 1000.0;
}

long get_free_memory() {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[256];
    long mem = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %ld kB", &mem);
            break;
        }
    }
    fclose(f);
    return mem;
}

float get_cpu_load() {
    long double a[4], b[4];
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1.0;
    fscanf(fp, "%*s %Lf %Lf %Lf %Lf", &a[0], &a[1], &a[2], &a[3]);
    fclose(fp);
    usleep(200000);
    fp = fopen("/proc/stat", "r");
    if (!fp) return -1.0;
    fscanf(fp, "%*s %Lf %Lf %Lf %Lf", &b[0], &b[1], &b[2], &b[3]);
    fclose(fp);

    long double loadavg = ((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) /
                          ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3])) * 100.0;
    return (float)loadavg;
}

// --- MQTT Functions ---
void init_mqtt() {
    mosquitto_lib_init();
    mosq = mosquitto_new("OrangePi_Guard_" STUDENT_ID, true, NULL);
    mosquitto_will_set(mosq, "telemetry/" STUDENT_ID "/home", strlen("{\"status\":\"OFFLINE\"}"), "{\"status\":\"OFFLINE\"}", 1, true);

    if (strlen(mqtt_user) > 0 && strlen(mqtt_pass) > 0) {
        mosquitto_username_pw_set(mosq, mqtt_user, mqtt_pass);
    }
    if (mosquitto_connect(mosq, LAPTOP_HOSTNAME, 1883, 60) != MOSQ_ERR_SUCCESS) {
        printf("[MQTT] Warning: Could not connect to broker at %s\n", LAPTOP_HOSTNAME);
    } else {
        printf("[MQTT] Connected to broker successfully.\n");
        mosquitto_loop_start(mosq);
    }
}

void publish_mqtt_data(int count) {
    if (!mosq) return;
    char payload[256];
    char time_buf[64];
    time_t now = time(NULL);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    snprintf(payload, sizeof(payload), "{\"student_id\":\"%s\", \"count\":%d, \"timestamp\":\"%s\"}", STUDENT_ID, count, time_buf);
    mosquitto_publish(mosq, NULL, "persons/" STUDENT_ID "/home", strlen(payload), payload, 1, false);

    // Part 4: Emergency MQTT for Guard Mode
    if (guard_mode == 1 && count > 0) {
        mosquitto_publish(mosq, NULL, "alarm/" STUDENT_ID "/home", strlen(payload), payload, 1, false);
    }

    snprintf(payload, sizeof(payload), "{\"student_id\":\"%s\", \"cpu_temp\":%.1f, \"timestamp\":\"%s\"}", STUDENT_ID, get_cpu_temp(), time_buf);
    mosquitto_publish(mosq, NULL, "telemetry/" STUDENT_ID "/home", strlen(payload), payload, 1, false);
}

// --- Email Alert Function ---
void check_and_send_alert(int new_count, const char *stream_ip) {
    time_t now = time(NULL);
    
    // Part 4: If guard mode is active, override the 30s debounce to send an immediate alert (5s hard limit to prevent Gmail ban)
    int debounce_time = (guard_mode == 1) ? 5 : 30;

    if (difftime(now, last_email_time) >= debounce_time) {
        last_email_time = now;
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
            "wget -q -O /tmp/alert.jpg http://%s:5000/snapshot && "
            "mpack -s 'Alert: %d person(s) at %s | Temp: %.1f C' /tmp/alert.jpg %s &",
            stream_ip, new_count, time_buf, get_cpu_temp(), alert_email);
        system(cmd);
    }
}

// --- Watchdog and Thermal Management Thread (Part 4) ---
void *watchdog_and_thermal_thread(void *arg) {
    while(1) {
        time_t now = time(NULL);
        
        // 1. Software Watchdog: Check if 30 seconds have passed without a frame heartbeat
        if (last_frame_time > 0 && difftime(now, last_frame_time) > 30) {
            if (difftime(now, last_watchdog_email) > 60) { // Limit email spam
                last_watchdog_email = now;
                printf("[Watchdog] Camera tampering or drop detected! Sending email and restarting...\n");
                
                char alert_cmd[512];
                snprintf(alert_cmd, sizeof(alert_cmd), 
                    "echo 'No frame received for 30 seconds. System restarting.' > /tmp/watchdog.txt && "
                    "mpack -s 'Watchdog Alert: Camera Tampering' /tmp/watchdog.txt %s &", alert_email);
                system(alert_cmd);
                
                system("sudo systemctl restart webserver.service &"); // Restarts the service
            }
        }

        // 2. Adaptive Thermal Management
        float temp = get_cpu_temp();
        if (temp > 65.0 && thermal_throttle == 0) {
            thermal_throttle = 1;
            
            char temp_cmd[512];
            snprintf(temp_cmd, sizeof(temp_cmd), 
                "echo 'CPU Temp is %.1f. Automatically reducing FPS/Resolution.' > /tmp/thermal.txt && "
                "mpack -s 'Thermal Alert' /tmp/thermal.txt %s &", temp, alert_email);
            system(temp_cmd);
            
        } else if (temp < 60.0 && thermal_throttle == 1) {
            thermal_throttle = 0; // CPU cooled down, restore performance
        }

        sleep(5);
    }
    return NULL;
}

// --- OpenSSL Setup ---
SSL_CTX *create_ssl_context() {
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    return ctx;
}

void configure_ssl_context(SSL_CTX *ctx) {
    SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM);
}

// --- HTTP 301 Redirect Thread ---
void *http_redirect_thread(void *arg) {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) pthread_exit(NULL);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(HTTP_PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);
    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) >= 0) {
            char buffer[1024] = {0};
            read(client_socket, buffer, sizeof(buffer));
            struct sockaddr_in local_addr;
            socklen_t local_addr_len = sizeof(local_addr);
            getsockname(client_socket, (struct sockaddr *)&local_addr, &local_addr_len);
            char redirect_response[512];
            sprintf(redirect_response,
                "HTTP/1.1 301 Moved Permanently\r\nLocation: https://%s:%d/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                inet_ntoa(local_addr.sin_addr), HTTPS_PORT);
            write(client_socket, redirect_response, strlen(redirect_response));
            close(client_socket);
        }
    }
    return NULL;
}

// --- Main HTTPS Server ---
int main() {
    load_secrets();
    init_db();

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    SSL_CTX *ctx = create_ssl_context();
    configure_ssl_context(ctx);

    init_mqtt();
    add_history_record(0);

    pthread_t redirect_tid;
    pthread_create(&redirect_tid, NULL, http_redirect_thread, NULL);

    pthread_t watchdog_tid;
    pthread_create(&watchdog_tid, NULL, watchdog_and_thermal_thread, NULL);

    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) exit(EXIT_FAILURE);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(HTTPS_PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);

    printf("[HTTPS Server] Running on port %d\n", HTTPS_PORT);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;

        char *client_ip = inet_ntoa(address.sin_addr);
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_socket);

        if (SSL_accept(ssl) > 0) {
            char buffer[2048] = {0};
            int bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);

            // FIX: Ensure heartbeat and command endpoints are not delayed by SSL read blocking
            if (bytes_read > 0 && strstr(buffer, "POST") != NULL && 
                strstr(buffer, "\"count\":") == NULL && 
                strstr(buffer, "\"state\":") == NULL &&
                strstr(buffer, "\"cmd\":") == NULL &&
                strstr(buffer, "heartbeat") == NULL) { 
                usleep(50000); 
                SSL_read(ssl, buffer + bytes_read, sizeof(buffer) - 1 - bytes_read);
            }

            // ========================================================
            // ENDPOINT ROUTING
            // ========================================================
            if (strncmp(buffer, "GET /api/v1/telemetry", 21) == 0) {
                char json[256], response[512];
                sprintf(json, "{\"cpu_temp\": %.1f, \"free_memory_kb\": %ld, \"cpu_load_pct\": %.1f, \"thermal_throttle\": %d}", get_cpu_temp(), get_free_memory(), get_cpu_load(), thermal_throttle);
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n%s", json);
                SSL_write(ssl, response, strlen(response));
            }
            else if (strncmp(buffer, "GET /api/v1/persons", 19) == 0) {
                time_t now = time(NULL);
                char time_buf[32];
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
                char json[128], response[512];
                sprintf(json, "{\"timestamp\": \"%s\", \"person_count\": %d}", time_buf, current_persons);
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n%s", json);
                SSL_write(ssl, response, strlen(response));
            }
            else if (strncmp(buffer, "GET /api/v1/stream", 18) == 0) {
                char response[512];
                sprintf(response, "HTTP/1.1 302 Found\r\nLocation: http://%s:5000/video_feed\r\nConnection: close\r\n\r\n", client_ip);
                SSL_write(ssl, response, strlen(response));
            }
            else if (strncmp(buffer, "GET /api/v1/history", 19) == 0) {
                char json[1024] = "[";
                for (int i = 0; i < history_count; i++) {
                    char item[128];
                    sprintf(item, "{\"timestamp\": \"%s\", \"person_count\": %d}%s",
                            history[i].timestamp, history[i].count, (i == history_count - 1) ? "" : ",");
                    strcat(json, item);
                }
                strcat(json, "]");
                char response[1500];
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n%s", json);
                SSL_write(ssl, response, strlen(response));
            }
            // Part 4: SQLite Database Report Endpoint
            else if (strncmp(buffer, "GET /api/v1/total_detections", 28) == 0) {
                int total_det = 0;
                sqlite3_stmt *stmt;
                if (sqlite3_prepare_v2(db, "SELECT SUM(count) FROM history;", -1, &stmt, 0) == SQLITE_OK) {
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        total_det = sqlite3_column_int(stmt, 0);
                    }
                    sqlite3_finalize(stmt);
                }
                char json[128], response[512];
                sprintf(json, "{\"total_historical_detections\": %d}", total_det);
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n%s", json);
                SSL_write(ssl, response, strlen(response));
            }
            // Part 4: Watchdog Heartbeat Endpoint
            else if (strncmp(buffer, "POST /api/v1/heartbeat", 22) == 0) {
                last_frame_time = time(NULL); // Reset the 30-second timer
                char response[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"status\":\"alive\"}";
                SSL_write(ssl, response, strlen(response));
            }
            else if (strncmp(buffer, "POST /api/v1/command", 20) == 0) {
                if (strstr(buffer, "\"cmd\":\"reboot\"") || strstr(buffer, "\"cmd\": \"reboot\"")) {
                    char response[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n{\"status\":\"executing\", \"cmd\":\"reboot\"}";
                    SSL_write(ssl, response, strlen(response));
                    sync();
                    system("sudo reboot");
                } else {
                    char response[] = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"Unknown command\"}";
                    SSL_write(ssl, response, strlen(response));
                }
            }
            // Part 4: Guard Mode Toggle Endpoint
            else if (strncmp(buffer, "POST /api/v1/guard", 18) == 0) {
                char *state_ptr = strstr(buffer, "\"state\":");
                if (state_ptr) {
                    guard_mode = atoi(state_ptr + 8);
                    printf("[Guard Mode] Toggled to: %d\n", guard_mode);
                }
                char response[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}";
                SSL_write(ssl, response, strlen(response));
            }
            else if (strncmp(buffer, "POST /api/v1/update_persons", 27) == 0) {
                last_frame_time = time(NULL); // Also counts as a heartbeat

                char *count_ptr = strstr(buffer, "\"count\":");
                if (count_ptr) {
                    int new_count = atoi(count_ptr + 8);

                    // FIX: Ensure immediate alert on detection if Guard Mode is active, or standard alert on first detection
                    if ((new_count > 0 && current_persons == 0) || (guard_mode == 1 && new_count > 0)) {
                        check_and_send_alert(new_count, client_ip);
                    }

                    if (new_count != current_persons) {
                        current_persons = new_count;
                        add_history_record(current_persons);
                        publish_mqtt_data(current_persons);
                    }
                }
                char response[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}";
                SSL_write(ssl, response, strlen(response));
            }
            // DEFAULT: HTML Webpage (Updated with Guard Mode UI)
            else {
                char html_response[2500];
                sprintf(html_response,
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                    "<html><head><title>MohammadrezaSharifi - 401101932</title>"
                    "<meta http-equiv='refresh' content='2'>"
                    "<script>function toggleGuard(){"
                    "var newState = %d === 1 ? 0 : 1;"
                    "fetch('/api/v1/guard', {method: 'POST', body: JSON.stringify({state: newState})})"
                    ".then(() => location.reload());}</script>"
                    "</head>"
                    "<body style='font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4;'>"
                    "<h2>Smart Security System (HTTPS)</h2>"
                    "<img src='http://%s:5000/video_feed' width='640' height='480' style='border: 2px solid black;' />"
                    "<div style='margin-top: 20px; font-size: 1.2em;'>"
                    "<p><b>CPU Temp:</b> %.1f &deg;C | <b>CPU Load:</b> %.1f%% | <b>Free RAM:</b> %ld kB</p>"
                    "<p><b>Persons Detected:</b> %d</p>"
                    "<button onclick='toggleGuard()' style='padding:10px; font-size:16px; background-color:%s; color:white; border:none; border-radius:5px;'>"
                    "%s</button>"
                    "</div></body></html>",
                    guard_mode, client_ip, get_cpu_temp(), get_cpu_load(), get_free_memory(), current_persons, 
                    guard_mode == 1 ? "red" : "green", guard_mode == 1 ? "Guard Mode ACTIVE (Click to Disable)" : "Guard Mode OFF (Click to Enable)");
                SSL_write(ssl, html_response, strlen(html_response));
            }
        }
        usleep(50000);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_socket);
    }
    close(server_fd);
    SSL_CTX_free(ctx);
    return 0;
}