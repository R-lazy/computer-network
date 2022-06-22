#ifndef TINYSERVER_HTTP_CONN_H
#define TINYSERVER_HTTP_CONN_H

#include <string>
#include <array>
#include <unordered_map>
#include <regex>
#include <random>

#include <ctime>

#include <dirent.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include "common.h"

class HttpConn;

enum LINE_STATE {
    LINE_OPEN = 0, LINE_OK, LINE_BAD,
};
enum HTTP_CHECK_STATE {
    REQUEST = 0, HEADER, CONTENT    // REQ_OK, also DATA_OK
};
enum HTTP_CODE {
    NO_REQUEST = 0,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
};
enum CONTENT_TYPE {
    HTML = 0, IMG_JPG, IMG_PNG,
};
enum HTTP_METHOD {
    GET = 0, POST,
};

static std::unordered_map<int, const char *> status_code_map = {
        {200, "OK"},
        {400, "Bad Request"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {500, "Internal Server Error"}
};


static std::regex req_re("^(GET|POST)\\s([^\\s]+)\\s(HTTP\\/1\\.1)$", std::regex::icase);
static std::default_random_engine file_no_e(time(nullptr));
static std::uniform_int_distribution<int> distribution(0, 0x7fffffff);

// http conn class
class HttpConn : public Runner {
public:
    // class interface
    HttpConn();
    ~HttpConn() = default;

    void init(int remote_fd, const sockaddr_in &address, int epoll_fd);
    void closeConn();

    bool readReqToBuf();    // read http request from client
    bool prepareWrite(HTTP_CODE http_code);
    bool writeResp();

    void run() final;       // parse http request in buffer
    static void addResourceFile(const char *filename);
    static void prepareResource();

private:
    // http common function
    void init();
    HTTP_CODE parseReq();
    LINE_STATE parseLine();
    HTTP_CODE parseReqLine(char *line);
    HTTP_CODE parseHeaders(char *line);
    HTTP_CODE parseContent();
    HTTP_CODE prepareFile(const char *filename);
    inline char *getLine();

    void addCRLF();
    void addStatusLine(const char *version, const char *status_code);
    void addHeader(const char *key, const char *value);

private:
    // response common function
    void unmap();

private:
    // http common information
    int m_epoll_fd;
    int m_remote_fd;

    // must take sure that big enough read buffer size.
    static constexpr int read_buf_size = 2048;
    static constexpr int write_buf_size = 2048;

    // store complete http request
    char m_read_buf[read_buf_size];
    char m_write_header_buf[write_buf_size];
    int m_header_size;
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_write_vec[2];

    ssize_t m_line_ind;     // index point to line
    ssize_t m_read_ind;     // index where read buffer has been checked
    ssize_t m_read_end;     // index which points to end of read buffer
    int m_write_vec_count;
    ssize_t m_byte_to_send;
    ssize_t m_byte_have_send;

private:
    // header information
    char *m_http_method;
    char *m_src_path;
    char *m_http_version;
    CONTENT_TYPE m_content_type;

    int m_content_length;
    // std::regex req_re;

    HTTP_CHECK_STATE m_check_state;

    static std::vector<std::string> resource_filename;
};

#endif //TINYSERVER_HTTP_CONN_H
