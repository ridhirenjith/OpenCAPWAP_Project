/*******************************************************************************************
 * AC HTTP API Server Implementation                                                      *
 * Provides REST API endpoints for AC management and monitoring                           *
 *******************************************************************************************/

#include "AC_HTTP_API.h"
#include "CWLog.h"
#include "CWStevens.h"
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

static HTTPAPIServer http_api_server = {0};
static CWThreadMutex message_flow_mutex = PTHREAD_MUTEX_INITIALIZER;

void AC_Message_Flow_Init(void) {
	http_api_server.msg_flow_buf.write_index = 0;
	http_api_server.msg_flow_buf.count = 0;
	http_api_server.msg_flow_buf.initialized = CW_TRUE;
}

void AC_Message_Flow_Log(int wtp_index, int message_type, int direction, const char *description) {
	if (!http_api_server.msg_flow_buf.initialized) {
		return;
	}

	CWThreadMutexLock(&message_flow_mutex);

	int idx = http_api_server.msg_flow_buf.write_index;
	CAPWAPMessageFlow *flow = &http_api_server.msg_flow_buf.messages[idx];
	
	flow->wtp_index = wtp_index;
	flow->message_type = message_type;
	flow->direction = direction;
	flow->timestamp = (uint32_t)time(NULL);
	flow->timestamp_ms = 0;
	
	strncpy(flow->description, description ? description : "Unknown", MAX_MESSAGE_DESC - 1);
	flow->description[MAX_MESSAGE_DESC - 1] = '\0';

	http_api_server.msg_flow_buf.write_index = (idx + 1) % MAX_MESSAGE_FLOWS;
	if (http_api_server.msg_flow_buf.count < MAX_MESSAGE_FLOWS) {
		http_api_server.msg_flow_buf.count++;
	}

	CWThreadMutexUnlock(&message_flow_mutex);
}

char* getAddressString(CWNetworkLev4Address *addr, char *buffer, int buflen) {
	char str[256];
	char *result = sock_ntop_r((struct sockaddr*)addr, str);
	if (result) {
		snprintf(buffer, buflen, "%s", str);
		return buffer;
	}
	snprintf(buffer, buflen, "UNKNOWN");
	return buffer;
}

void AC_HTTP_API_BuildACInfoJSON(char *buffer, int buffer_size) {
	snprintf(buffer, buffer_size,
		"{"
		"\"ac_name\":\"%s\","
		"\"hw_version\":%d,"
		"\"sw_version\":%d,"
		"\"active_wtps\":%d,"
		"\"max_wtps\":%d,"
		"\"active_stations\":%d,"
		"\"interfaces_count\":%d"
		"}",
		gACName ? gACName : "AC",
		gACHWVersion,
		gACSWVersion,
		gActiveWTPs,
		gMaxWTPs,
		gActiveStations,
		gInterfacesCount
	);
}

void AC_HTTP_API_BuildMessageFlowJSON(char *buffer, int buffer_size, int wtp_filter) {
	int i, offset = 0, msg_idx;
	char *buf = buffer;
	int remaining = buffer_size;
	int messages_to_show = http_api_server.msg_flow_buf.count;
	
	offset += snprintf(buf + offset, remaining - offset, "[");
	
	CWThreadMutexLock(&message_flow_mutex);
	
	for (i = 0; i < messages_to_show && offset < buffer_size - 200; i++) {
		msg_idx = (http_api_server.msg_flow_buf.write_index - messages_to_show + i + MAX_MESSAGE_FLOWS) % MAX_MESSAGE_FLOWS;
		CAPWAPMessageFlow *flow = &http_api_server.msg_flow_buf.messages[msg_idx];
		
		if (wtp_filter >= 0 && flow->wtp_index != wtp_filter) {
			continue;
		}
		
		if (offset > 1) {
			offset += snprintf(buf + offset, remaining - offset, ",");
		}
		
		offset += snprintf(buf + offset, remaining - offset,
			"{"
			"\"timestamp\":%u,"
			"\"wtp_index\":%d,"
			"\"message_type\":%d,"
			"\"direction\":%d,"
			"\"description\":\"%s\""
			"}",
			flow->timestamp,
			flow->wtp_index,
			flow->message_type,
			flow->direction,
			flow->description
		);
	}
	
	CWThreadMutexUnlock(&message_flow_mutex);
	
	offset += snprintf(buf + offset, remaining - offset, "]");
}

void AC_HTTP_API_BuildWTPInfoJSON(char *buffer, int buffer_size) {
	int i, offset = 0;
	char *buf = buffer;
	int remaining = buffer_size;
	char addr_str[256];

	offset += snprintf(buf + offset, remaining - offset, "[");

	CWThreadMutexLock(&gWTPsMutex);
	
	for (i = 0; i < CW_MAX_WTP; i++) {
		if (gWTPs[i].isNotFree) {
			if (offset > 1) {
				offset += snprintf(buf + offset, remaining - offset, ",");
			}

			getAddressString(&gWTPs[i].address, addr_str, sizeof(addr_str));

			/* Defensive check: verify radioCount is reasonable */
			int radio_count = (gWTPs[i].WTPProtocolManager.radiosInfo.radioCount > 0 && 
							   gWTPs[i].WTPProtocolManager.radiosInfo.radioCount <= 16) ? 
							  gWTPs[i].WTPProtocolManager.radiosInfo.radioCount : 0;

			offset += snprintf(buf + offset, remaining - offset,
				"{"
				"\"index\":%d,"
				"\"address\":\"%s\","
				"\"state\":%d,"
				"\"is_in_use\":%s,"
				"\"radios_count\":%d"
				"}",
				i,
				addr_str,
				gWTPs[i].currentState,
				gWTPs[i].isNotFree ? "true" : "false",
				radio_count
			);

			if (remaining - offset < 100) {
				break;
			}
		}
	}

	CWThreadMutexUnlock(&gWTPsMutex);
	
	offset += snprintf(buf + offset, remaining - offset, "]");
}

void AC_HTTP_API_BuildWTPDetailJSON(int wtp_index, char *buffer, int buffer_size) {
	char *buf = buffer;
	int offset = 0, i, j;
	char addr_str[256], data_addr_str[256];

	if (wtp_index < 0 || wtp_index >= CW_MAX_WTP || !gWTPs[wtp_index].isNotFree) {
		snprintf(buffer, buffer_size, "{\"error\":\"WTP not found\"}");
		return;
	}

	CWThreadMutexLock(&gWTPsMutex);
	
	/* Double-check WTP is still valid after acquiring lock */
	if (!gWTPs[wtp_index].isNotFree) {
		CWThreadMutexUnlock(&gWTPsMutex);
		snprintf(buffer, buffer_size, "{\"error\":\"WTP disconnected\"}");
		return;
	}

	CWWTPManager *wtp = &gWTPs[wtp_index];
	getAddressString(&wtp->address, addr_str, sizeof(addr_str));
	getAddressString(&wtp->dataaddress, data_addr_str, sizeof(data_addr_str));
	
	offset += snprintf(buf + offset, buffer_size - offset,
		"{"
		"\"index\":%d,"
		"\"address\":\"%s\","
		"\"data_address\":\"%s\","
		"\"state\":%d,"
		"\"path_mtu\":%d,"
		"\"radios\":["
		,
		wtp_index,
		addr_str,
		data_addr_str,
		wtp->currentState,
		wtp->pathMTU
	);

	/* Safely access radio info with defensive checks */
	int radio_count = (wtp->WTPProtocolManager.radiosInfo.radioCount > 16) ? 16 : wtp->WTPProtocolManager.radiosInfo.radioCount;
	
	for (i = 0; i < radio_count && offset < buffer_size - 200; i++) {
		if (i > 0) {
			offset += snprintf(buf + offset, buffer_size - offset, ",");
		}
		
		offset += snprintf(buf + offset, buffer_size - offset,
			"{"
			"\"radio_id\":%d,"
			"\"interfaces\":["
			, i
		);

		int iface_count = 0;
		for (j = 0; j < 16 && offset < buffer_size - 200; j++) {
			/* Defensive NULL checks to prevent crash from freed memory */
			if (wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].BSSID != NULL) {
				if (iface_count > 0) {
					offset += snprintf(buf + offset, buffer_size - offset, ",");
				}
				
				/* Safely access SSID with NULL check */
				const char *ssid = wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].SSID;
				offset += snprintf(buf + offset, buffer_size - offset,
					"{"
					"\"wlan_id\":%d,"
					"\"ssid\":\"%s\","
					"\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\""
					"}",
					j,
					(ssid != NULL) ? ssid : "N/A",
					(unsigned char)wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].BSSID[0],
					(unsigned char)wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].BSSID[1],
					(unsigned char)wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].BSSID[2],
					(unsigned char)wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].BSSID[3],
					(unsigned char)wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].BSSID[4],
					(unsigned char)wtp->WTPProtocolManager.radiosInfo.radiosInfo[i].gWTPPhyInfo.interfaces[j].BSSID[5]
				);
				iface_count++;
			}
		}

		offset += snprintf(buf + offset, buffer_size - offset, "]}");
	}

	offset += snprintf(buf + offset, buffer_size - offset, "]}");

	CWThreadMutexUnlock(&gWTPsMutex);
}

void AC_HTTP_API_SendResponse(int client_socket, int status_code, const char *content_type, const char *body) {
	char *response = malloc(HTTP_BUFFER_SIZE);
	if (!response) {
		return;
	}

	int response_len = snprintf(response, HTTP_BUFFER_SIZE,
		"HTTP/1.1 %d OK\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %zu\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n"
		"\r\n"
		"%s",
		status_code,
		content_type,
		strlen(body),
		body
	);

	if (response_len > 0 && response_len < HTTP_BUFFER_SIZE) {
		send(client_socket, response, response_len, 0);
	}

	free(response);
}

void AC_HTTP_API_SendHTMLFile(int client_socket) {
	FILE *file = fopen("gui/index.html", "r");
	
	if (!file) {
		const char *error = "<html><body><h1>Error 404: GUI not found</h1><p>The web interface file is missing.</p></body></html>";
		AC_HTTP_API_SendResponse(client_socket, 404, "text/html", error);
		CWDebugLog("GUI file not found: gui/index.html");
		return;
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (file_size <= 0) {
		fclose(file);
		const char *error = "<html><body><h1>Error: Empty GUI file</h1></body></html>";
		AC_HTTP_API_SendResponse(client_socket, 500, "text/html", error);
		return;
	}

	char *file_content = malloc(file_size + 1);
	if (!file_content) {
		fclose(file);
		const char *error = "{\"error\":\"Server memory error\"}";
		AC_HTTP_API_SendResponse(client_socket, 500, "application/json", error);
		return;
	}

	size_t read_size = fread(file_content, 1, file_size, file);
	fclose(file);
	
	if (read_size <= 0) {
		free(file_content);
		const char *error = "<html><body><h1>Error: Cannot read GUI file</h1></body></html>";
		AC_HTTP_API_SendResponse(client_socket, 500, "text/html", error);
		return;
	}

	file_content[read_size] = '\0';

	char *response = malloc(4096);
	if (!response) {
		free(file_content);
		const char *error = "{\"error\":\"Server memory error\"}";
		AC_HTTP_API_SendResponse(client_socket, 500, "application/json", error);
		return;
	}

	int response_len = snprintf(response, 4096,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Content-Length: %zu\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n"
		"\r\n",
		read_size
	);

	if (response_len > 0 && response_len < 4096) {
		if (send(client_socket, response, response_len, 0) > 0) {
			send(client_socket, file_content, read_size, 0);
		}
	}
	
	free(response);
	free(file_content);
}

void AC_HTTP_API_HandleRequest(int client_socket, const char *request) {
	char method[16], path[256];
	char *buffer;
	int wtp_index = -1, wtp_filter = -1;

	buffer = (char *)malloc(HTTP_BUFFER_SIZE);
	if (!buffer) {
		const char *error = "{\"error\":\"Server memory error\"}";
		AC_HTTP_API_SendResponse(client_socket, 500, "application/json", error);
		return;
	}

	if (sscanf(request, "%15s %255s", method, path) != 2) {
		snprintf(buffer, HTTP_BUFFER_SIZE, "{\"error\":\"Invalid request\"}");
		AC_HTTP_API_SendResponse(client_socket, 400, "application/json", buffer);
		free(buffer);
		return;
	}

	if (strstr(path, "/api/ac/info")) {
		AC_HTTP_API_BuildACInfoJSON(buffer, HTTP_BUFFER_SIZE);
		AC_HTTP_API_SendResponse(client_socket, 200, "application/json", buffer);
	}
	else if (strcmp(path, "/api/wtps") == 0) {
		AC_HTTP_API_BuildWTPInfoJSON(buffer, HTTP_BUFFER_SIZE);
		AC_HTTP_API_SendResponse(client_socket, 200, "application/json", buffer);
	}
	else if (sscanf(path, "/api/wtps/%d", &wtp_index) == 1) {
		AC_HTTP_API_BuildWTPDetailJSON(wtp_index, buffer, HTTP_BUFFER_SIZE);
		AC_HTTP_API_SendResponse(client_socket, 200, "application/json", buffer);
	}
	else if (strcmp(path, "/api/message-flows") == 0) {
		AC_HTTP_API_BuildMessageFlowJSON(buffer, HTTP_BUFFER_SIZE, -1);
		AC_HTTP_API_SendResponse(client_socket, 200, "application/json", buffer);
	}
	else if (sscanf(path, "/api/message-flows?wtp=%d", &wtp_filter) == 1 || 
	         sscanf(path, "/api/message-flows?wtp=%d", &wtp_filter) == 1) {
		AC_HTTP_API_BuildMessageFlowJSON(buffer, HTTP_BUFFER_SIZE, wtp_filter);
		AC_HTTP_API_SendResponse(client_socket, 200, "application/json", buffer);
	}
	else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
		AC_HTTP_API_SendHTMLFile(client_socket);
	}
	else {
		snprintf(buffer, HTTP_BUFFER_SIZE, "{\"error\":\"Not found\"}");
		AC_HTTP_API_SendResponse(client_socket, 404, "application/json", buffer);
	}

	free(buffer);
}

CW_THREAD_RETURN_TYPE AC_HTTP_API_MainLoop(void *arg) {
	struct sockaddr_in client_addr;
	int client_socket;
	socklen_t addr_len = sizeof(client_addr);
	char *buffer;
	int bytes_received;
	struct timeval timeout;

	buffer = (char *)malloc(HTTP_BUFFER_SIZE);
	if (!buffer) {
		CWDebugLog("Failed to allocate HTTP API buffer");
		return NULL;
	}

	CWDebugLog("HTTP API Server started on port %d", http_api_server.port);

	while (http_api_server.running) {
		client_socket = accept(http_api_server.server_socket, (struct sockaddr *)&client_addr, &addr_len);
		
		if (client_socket < 0) {
			if (errno != EINTR) {
				usleep(100);
			}
			continue;
		}

		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		memset(buffer, 0, HTTP_BUFFER_SIZE);
		bytes_received = recv(client_socket, buffer, HTTP_BUFFER_SIZE - 1, 0);
		
		if (bytes_received > 0) {
			buffer[bytes_received] = '\0';
			AC_HTTP_API_HandleRequest(client_socket, buffer);
		} else if (bytes_received == 0) {
			CWDebugLog("HTTP client disconnected");
		} else if (bytes_received < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNRESET) {
				CWDebugLog("HTTP recv error: %d", errno);
			}
		}

		close(client_socket);
	}

	free(buffer);
	return NULL;
}

CWBool AC_HTTP_API_Init(int port) {
	struct sockaddr_in server_addr;
	int reuse_addr = 1;

	http_api_server.server_socket = socket(AF_INET, SOCK_STREAM, 0);
	
	if (http_api_server.server_socket < 0) {
		CWDebugLog("Failed to create HTTP API server socket");
		return CW_FALSE;
	}

	if (setsockopt(http_api_server.server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
		CWDebugLog("Failed to set socket option");
		close(http_api_server.server_socket);
		return CW_FALSE;
	}

	http_api_server.port = port;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	if (bind(http_api_server.server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		CWDebugLog("Failed to bind HTTP API server socket");
		close(http_api_server.server_socket);
		return CW_FALSE;
	}

	if (listen(http_api_server.server_socket, MAX_HTTP_CONNECTIONS) < 0) {
		CWDebugLog("Failed to listen on HTTP API server socket");
		close(http_api_server.server_socket);
		return CW_FALSE;
	}

	http_api_server.running = CW_TRUE;
	AC_Message_Flow_Init();
	CWDebugLog("HTTP API Server initialized on port %d", port);

	return CW_TRUE;
}

CWBool AC_HTTP_API_Start(void) {
	if (http_api_server.server_socket <= 0) {
		return CW_FALSE;
	}

	if (!CWCreateThread(&http_api_server.http_thread, AC_HTTP_API_MainLoop, NULL)) {
		CWDebugLog("Failed to create HTTP API server thread");
		return CW_FALSE;
	}

	CWDebugLog("HTTP API Server thread started");
	return CW_TRUE;
}

void AC_HTTP_API_Stop(void) {
	http_api_server.running = CW_FALSE;
	
	if (http_api_server.server_socket > 0) {
		close(http_api_server.server_socket);
		http_api_server.server_socket = 0;
	}

	CWDebugLog("HTTP API Server stopped");
}
