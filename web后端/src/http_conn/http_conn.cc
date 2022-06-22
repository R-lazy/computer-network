#include <string>
#include <regex>

#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "http_conn.h"
#include "common.h"

std::vector<std::string> HttpConn::resource_filename;

HttpConn::HttpConn() = default;

void HttpConn::init(int remote_fd, const sockaddr_in &address, int epoll_fd) {
    m_epoll_fd = epoll_fd;
    m_remote_fd = remote_fd;
    addToEpoll(epoll_fd, remote_fd);
}

void HttpConn::init() {
    memset(m_read_buf, 0, read_buf_size);
    memset(m_write_header_buf, 0, write_buf_size);
    m_header_size = 0;
    m_file_address = nullptr;
    m_line_ind = 0;
    m_read_ind = 0;
    m_read_end = 0;
    m_write_vec_count = 0;
    m_byte_to_send = 0;
    m_byte_have_send = 0;
    m_content_length = 0;
    m_check_state = REQUEST;
}

void HttpConn::closeConn() {
    close(m_remote_fd);
    removeFromEpoll(m_epoll_fd, m_remote_fd);
}

/*
 * read http request from client.
 * this will only be activated by EPOLLIN
 * for parsing client http request.
 *
 * return true if parse ok or need further parse
 * return false if bad http request
 */
bool HttpConn::readReqToBuf() {
    while (true) {
        ssize_t bytes = read(m_remote_fd,
                             m_read_buf + m_read_end,
                             read_buf_size - m_read_end);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // modFd(m_epoll_fd, m_remote_fd, EPOLLIN);
                // more things to read, waiting for next call.
                break;
            }
            return false;
        } else if (bytes == 0) {
            // request size might too big.
            return false;
        }
        m_read_end += bytes;
    }
    return true;
}

/*
 * prepare write vec
 */
bool HttpConn::prepareWrite(HTTP_CODE http_code) {
    switch (http_code) {
        case INTERNAL_ERROR:
            addStatusLine("HTTP/1.1", "500");
            addCRLF();
            break;
        case BAD_REQUEST:
            addStatusLine("HTTP/1.1", "400");
            addCRLF();
            break;
        case FORBIDDEN_REQUEST:
            addStatusLine("HTTP/1.1", "403");
            addCRLF();
            break;
        case NO_RESOURCE:
            addStatusLine("HTTP/1.1", "404");
            addCRLF();
            break;
        case FILE_REQUEST:
            addStatusLine("HTTP/1.1", "200");
            if (m_content_type == HTML)
                addHeader("Content-Type", "text/html");
            else if (m_content_type == IMG_JPG)
                addHeader("Content-Type", "image/jpeg");
            else if (m_content_type == IMG_PNG)
                addHeader("Content-Type", "image/png");
            if (m_file_stat.st_size != 0) {
                addHeader("Content-Length", std::to_string(m_file_stat.st_size).c_str());
                addCRLF();
                m_write_vec[0].iov_base = m_write_header_buf;
                m_write_vec[0].iov_len = m_header_size;
                m_write_vec[1].iov_base = m_file_address;
                m_write_vec[1].iov_len = m_file_stat.st_size;
                m_write_vec_count = 2;
                m_byte_to_send = m_header_size + m_file_stat.st_size;
                return true;
            }
            //addCRLF();
            break;
        default:
            return false;
    }
    m_write_vec[0].iov_base = m_write_header_buf;
    m_write_vec[0].iov_len = m_header_size;
    m_write_vec_count = 1;
    return true;
}

/*
 * write prepared data to client
 * called when register and trigger EPOLLOUT
 */
bool HttpConn::writeResp() {
    while (true) {
        ssize_t bytes = writev(m_remote_fd, m_write_vec, m_write_vec_count);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // modFd(m_epoll_fd, m_remote_fd, EPOLLOUT);
                return true;
            }
            // send file error
            unmap();
            return false;
        }

        m_byte_have_send += bytes;
        m_byte_to_send -= bytes;
        if (m_byte_have_send < m_write_vec[0].iov_len) {
            // vec[0] has been sent incompletely
            m_write_vec[0].iov_base = m_write_header_buf + m_byte_have_send;
            m_write_vec[0].iov_len = m_write_vec[0].iov_len - m_byte_have_send;
        } else {
            // vec[0] send complete
            m_write_vec[0].iov_len = 0;
            m_write_vec[1].iov_base = m_file_address +
                                      (m_byte_have_send - m_header_size);
            m_write_vec[1].iov_len = m_byte_to_send;
        }

        if (m_byte_to_send <= 0) {
            // send file success
            unmap();
            modFd(m_epoll_fd, m_remote_fd, EPOLLIN);
            init();

            // enable consistent connection
            // return true;

            // disable consistent connection
            // for preventing fd leaks
            // TODO: implement timer to close inactive connection
            //  to make consistent connection possible.
            return false;
        }
    }
}

/*
 * parse http request
 */
HTTP_CODE HttpConn::parseReq() {
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE http_state = NO_REQUEST;
    while ((line_state = parseLine()) == LINE_OK) {
        switch (m_check_state) {
            // except push state by parse* function
            case REQUEST:
                if (parseReqLine(getLine()) == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            case HEADER:
                if (parseHeaders(getLine()) == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            case CONTENT:
                // parse content, decide what kind of data to send.
                break;
            default:
                return BAD_REQUEST;
        }
    }

    if (line_state == LINE_BAD)
        return BAD_REQUEST;
    else
        return parseContent();
}

/*
 * parse one line and move m_read_ind for buffer
 */
LINE_STATE HttpConn::parseLine() {
    for (; m_read_ind < m_read_end; ++m_read_ind) {
        if (m_read_buf[m_read_ind] == '\r') {
            if (m_read_ind + 1 == m_read_end)
                return LINE_OPEN;
            if (m_read_buf[m_read_ind + 1] == '\n') {
                m_read_buf[m_read_ind++] = '\0';
                m_read_buf[m_read_ind++] = '\0';
                return LINE_OK;
            } else {
                return LINE_BAD;
            }
        } else if (m_read_buf[m_read_ind] == '\n') {
            if (m_read_ind >= 1 && m_read_buf[m_read_ind - 1] == '\r') {
                m_read_buf[m_read_ind - 1] = '\0';
                m_read_buf[m_read_ind++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/*
 * parse status line after a complete line parsed by parseContent()
 */
HTTP_CODE HttpConn::parseReqLine(char *line) {
    std::cmatch matcher;
    if (!std::regex_match(line, matcher, req_re))
        return BAD_REQUEST;

    m_http_method = line;
    *const_cast<char *>(matcher[1].second) = '\0';
    m_src_path = const_cast<char *>(matcher[2].first);
    *const_cast<char *>(matcher[2].second) = '\0';
    m_http_version = const_cast<char *>(matcher[3].first);

    m_check_state = HEADER;
    return NO_REQUEST;
}

/*
 * parse header of http request
 * such as content-type or content-length
 */
HTTP_CODE HttpConn::parseHeaders(char *line) {
    if (*line == '\0' && *(line + 1) == '\0') {
        m_check_state = CONTENT;
    } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
        line += 15;
        line += strspn(line, " \t");
        m_content_length = atoi(line);
    }
    return NO_REQUEST;
}

/*
 * parse content and return type of content that client expect
 */
HTTP_CODE HttpConn::parseContent() {
    if (m_content_length == 0) {
        if (strcmp(m_src_path, "/") == 0) {
            m_content_type = HTML;
            return prepareFile("index.html");
        } else if (strcmp(m_src_path, "/funny_box.html") == 0) {
            m_content_type = HTML;
            return prepareFile("funny_box.html");
        } else if (strcmp(m_src_path, "/random_funny") == 0) {
            std::string &filename = resource_filename[distribution(file_no_e) % resource_filename.size()];
            if (filename.substr(filename.size() - 4) == ".jpg")
                m_content_type = IMG_JPG;
            else if (filename.substr(filename.size() - 4) == ".png")
                m_content_type = IMG_PNG;
            return prepareFile(("funny_mystery_box/" + filename).c_str());
        } else {
            return NO_RESOURCE;
        }
    }
    // more things to do
    return BAD_REQUEST;
}

HTTP_CODE HttpConn::prepareFile(const char *filename) {
    if (stat(filename, &m_file_stat) < 0)
        return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    int fd = open(filename, O_RDONLY);
    m_file_address =
            reinterpret_cast<char *>(mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    return FILE_REQUEST;
}

/*
 * get one line from m_read_buf and move m_line_ind to next
 */
inline char *HttpConn::getLine() {
    char *t = m_read_buf + m_line_ind;
    m_line_ind += strlen(t) + 2;
    return t;
}

void HttpConn::unmap() {
    if (m_file_address != nullptr) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

/*
 * called after readReqToBuf()
 * parse http request
 */
void HttpConn::run() {
    HTTP_CODE code = parseReq();
    if (code == NO_REQUEST) {
        return;
    }

    if (!prepareWrite(code))
        closeConn();
    // prepared to write
    modFd(m_epoll_fd, m_remote_fd, EPOLLOUT);
}

// common functions
void HttpConn::addCRLF() {
    strcat(m_write_header_buf, "\r\n");
    m_header_size += 2;
}

void HttpConn::addStatusLine(const char *version, const char *status_code) {
    strcat(m_write_header_buf, version);
    strcat(m_write_header_buf, " ");
    strcat(m_write_header_buf, status_code);
    strcat(m_write_header_buf, " ");
    const char *status_msg = status_code_map[atoi(status_code)];
    strcat(m_write_header_buf, status_msg);
    m_header_size += strlen(version) + strlen(status_code) +
                     strlen(status_msg) + 2;
    addCRLF();
}

void HttpConn::addHeader(const char *key, const char *value) {
    strcat(m_write_header_buf, key);
    strcat(m_write_header_buf, ": ");
    strcat(m_write_header_buf, value);
    m_header_size += strlen(key) + strlen(value) + 2;
    addCRLF();
}

void HttpConn::addResourceFile(const char *filename) {
    resource_filename.emplace_back(filename);
}

void HttpConn::prepareResource() {
    DIR *resource_dir;
    if ((resource_dir = opendir("funny_mystery_box")) == nullptr)
        exit(1);
    struct dirent *ptr;
    while ((ptr = readdir(resource_dir)) != nullptr) {
        if (ptr->d_type == DT_REG)
            addResourceFile(ptr->d_name);
    }
    closedir(resource_dir);
}
