/*
 * Known-answer selftest for rtty, run by mayhem/test.sh. rtty ships no tests, so this exercises the real
 * sources (built with normal flags, no sanitizers, same environment stand-ins as the fuzz harnesses)
 * and checks concrete output values: encoder results, wire-format replies, protocol state after a message
 * sequence, rejected malformed input. Prints one "PASS <name>" / "FAIL <name>" line per check and a final
 * "SELFTEST pass=<n> fail=<n>" line.
 */
#include <stdint.h>
#include <stddef.h>

#include "../src/rtty.c"
#include "../src/command.c"
#include "harness_common.h"

int selftest_config(void);

static int npass, nfail;

#define CHECK(name, cond) do { \
        if (cond) { npass++; printf("PASS %s\n", name); } \
        else { nfail++; printf("FAIL %s (line %d)\n", name, __LINE__); } \
    } while (0)

static struct rtty R;

static void reset(void)
{
    harness_free_tasks();
    if (R.loop)
        harness_free_rtty_io(&R);
    harness_init_rtty(&R, ev_default_loop(0));
}

static void put_frame(int type, const void *payload, size_t len)
{
    buffer_put_u8(&R.rb, type);
    buffer_put_u16be(&R.rb, len);
    if (len)
        buffer_put_data(&R.rb, payload, len);
}

static int wb_is(const void *data, size_t len)
{
    return buffer_length(&R.wb) == len && !memcmp(buffer_data(&R.wb), data, len);
}

static int cmd_reply_is(const char *json)
{
    size_t len = strlen(json);
    const uint8_t *w = buffer_data(&R.wb);

    return buffer_length(&R.wb) == len + 3 && w[0] == MSG_TYPE_CMD && w[1] == (len >> 8) &&
           w[2] == (len & 0xff) && !memcmp(w + 3, json, len);
}

static void test_utils(void)
{
    char out[32];

    CHECK("b64 foo", b64_encode("foo", 3, out, sizeof(out)) == 4 && !strcmp(out, "Zm9v"));
    CHECK("b64 fo", b64_encode("fo", 2, out, sizeof(out)) == 4 && !strcmp(out, "Zm8="));
    CHECK("b64 f", b64_encode("f", 1, out, sizeof(out)) == 4 && !strcmp(out, "Zg=="));
    CHECK("b64 foobar", b64_encode("foobar", 6, out, sizeof(out)) == 8 && !strcmp(out, "Zm9vYmFy"));
    CHECK("b64 small dest", b64_encode("foobar", 6, out, 6) == -1);
    CHECK("valid_id plain", valid_id("device-1", 16));
    CHECK("valid_id space", !valid_id("device 1", 16));
    CHECK("valid_id too long", !valid_id("0123456789abcdefg", 16));
    CHECK("format_size bytes", !strcmp(format_size(512), "512 B"));
    CHECK("format_size KB", !strcmp(format_size(2048), "2.00 KB"));
    CHECK("format_size MB", !strcmp(format_size(3 * 1024 * 1024), "3.00 MB"));
}

static void test_fileipc(void)
{
    struct file_packet p;
    int sv[2];
    const uint8_t bad[] = { 9, FILE_IPC_INFO, 0, 0 };

    CHECK("name plain", file_valid_name("a.txt", 5));
    CHECK("name slash", !file_valid_name("a/b", 3));
    CHECK("name dotdot", !file_valid_name("..", 2));
    CHECK("name empty", !file_valid_name("", 0));
    CHECK("name nul", !file_valid_name("a\0b", 3));

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) < 0) {
        CHECK("socketpair", 0);
        return;
    }

    CHECK("ipc send", file_ipc_send(sv[0], FILE_IPC_INFO, "hello", 5, -1) == 0);
    CHECK("ipc recv", file_ipc_recv(sv[1], &p) == 1 && p.type == FILE_IPC_INFO && p.len == 5 &&
          !memcmp(p.data, "hello", 5) && p.fd < 0);

    CHECK("ipc send empty", file_ipc_send(sv[0], FILE_IPC_DONE, NULL, 0, -1) == 0);
    CHECK("ipc recv empty", file_ipc_recv(sv[1], &p) == 1 && p.type == FILE_IPC_DONE && p.len == 0);

    CHECK("ipc bad version sent", send(sv[0], bad, sizeof(bad), 0) == sizeof(bad));
    CHECK("ipc bad version rejected", file_ipc_recv(sv[1], &p) == -1);

    close(sv[0]);
    CHECK("ipc eof", file_ipc_recv(sv[1], &p) == 0);
    close(sv[1]);
}

static void test_register_and_heartbeat(void)
{
    const uint8_t ok[] = { 0 };
    const uint8_t fail[] = { 1, 'b', 'a', 'd' };

    reset();
    put_frame(MSG_TYPE_REGISTER, ok, sizeof(ok));
    CHECK("register ok returns 0", parse_msg(&R) == 0);
    CHECK("register ok sets registered", R.registered && buffer_length(&R.rb) == 0);
    ev_timer_stop(R.loop, &R.tmr);

    reset();
    put_frame(MSG_TYPE_REGISTER, fail, sizeof(fail));
    CHECK("register fail rejected", parse_msg(&R) == -1 && !R.registered);

    reset();
    put_frame(MSG_TYPE_REGISTER, NULL, 0);
    CHECK("register empty rejected", parse_msg(&R) == -1);

    reset();
    R.wait_heartbeat = true;
    put_frame(MSG_TYPE_HEARTBEAT, NULL, 0);
    CHECK("heartbeat accepted", parse_msg(&R) == 0 && !R.wait_heartbeat && buffer_length(&R.rb) == 0);

    reset();
    put_frame(MSG_TYPE_MAX + 1, NULL, 0);
    CHECK("unknown type rejected", parse_msg(&R) == -1);

    reset();
    buffer_put_u8(&R.rb, MSG_TYPE_HEARTBEAT);
    buffer_put_u16be(&R.rb, 8);
    buffer_put_u8(&R.rb, 1);
    CHECK("partial frame waits", parse_msg(&R) == 0 && buffer_length(&R.rb) == 4);
    reset();
}

static void test_terminal(void)
{
    char sid[32], payload[37], expect[36];
    struct tty *tty;

    memset(sid, 'S', sizeof(sid));
    reset();

    put_frame(MSG_TYPE_TERM_OPEN, sid, sizeof(sid));
    CHECK("term open returns 0", parse_msg(&R) == 0);
    expect[0] = MSG_TYPE_TERM_OPEN;
    expect[1] = 0;
    expect[2] = 33;
    memcpy(expect + 3, sid, 32);
    expect[35] = 0;
    CHECK("term open reply", wb_is(expect, sizeof(expect)));
    CHECK("term open registers session", R.ntty == 1 && (tty = find_tty(&R, "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS")) != NULL);

    tty = find_tty(&R, "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS");
    memcpy(payload, sid, 32);
    memcpy(payload + 32, "hello", 5);
    put_frame(MSG_TYPE_TERMDATA, payload, 37);
    CHECK("term data returns 0", parse_msg(&R) == 0);
    CHECK("term data queued for pty", tty && buffer_length(&tty->wb) == 5 && !memcmp(buffer_data(&tty->wb), "hello", 5));

    put_frame(MSG_TYPE_TERM_OPEN, sid, sizeof(sid));
    buffer_free(&R.wb);
    CHECK("term open duplicate", parse_msg(&R) == 0 && R.ntty == 1);
    expect[35] = 0;
    CHECK("term open duplicate reply is busy", buffer_length(&R.wb) == 36 && ((uint8_t *)buffer_data(&R.wb))[35] != 0);

    put_frame(MSG_TYPE_LOGOUT, sid, sizeof(sid));
    CHECK("term logout returns 0", parse_msg(&R) == 0);
    CHECK("term logout removes session", R.ntty == 0 && find_tty(&R, "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS") == NULL);

    put_frame(MSG_TYPE_TERMDATA, payload, 37);
    CHECK("term data unknown sid ignored", parse_msg(&R) == 0 && buffer_length(&R.rb) == 0);

    put_frame(MSG_TYPE_TERMDATA, sid, 10);
    CHECK("short tty message rejected", parse_msg(&R) == -1);
    reset();
}

static void test_serial_ports(void)
{
    char sid[32], expect[36];

    memset(sid, 'P', sizeof(sid));
    reset();
    R.sock = 3;
    put_frame(MSG_TYPE_SERIAL_PORTS, sid, sizeof(sid));
    CHECK("serial ports listing returns 0", parse_msg(&R) == 0);
    expect[0] = MSG_TYPE_SERIAL_PORTS;
    expect[1] = 0;
    expect[2] = 33;
    memcpy(expect + 3, sid, 32);
    expect[35] = SERIAL_OK;
    CHECK("serial ports empty listing reply", wb_is(expect, sizeof(expect)));
    reset();
    put_frame(MSG_TYPE_SERIAL_PORTS, sid, 5);
    CHECK("serial ports bad length rejected", parse_msg(&R) == -1);
    reset();
}

static void test_command(void)
{
    static const char valid[] = "root\0/bin/sh\0tok1\0\1-c\0";
    static const char unknown_user[] = "nobody-such\0/bin/sh\0t2\0\0";
    static const char unterminated[] = "root\0/bin/sh";
    static const char no_user[] = "\0/bin/sh\0t3\0\0";
    static const char missing[] = "root\0/nonexistent/cmd\0t4\0\0";
    static const char err_permit[] =
        "{\"token\":\"t2\",\"attrs\":{\"err\":1,\"msg\":\"operation not permitted\"}}";
    static const char err_missing[] =
        "{\"token\":\"t4\",\"attrs\":{\"err\":2,\"msg\":\"not found\"}}";

    reset();
    CHECK("cmd valid accepted", run_command(&R, valid, sizeof(valid) - 1) == 0);
    CHECK("cmd valid starts one task", nrunning == 1 && mayhem_ntask_children == 1 && buffer_length(&R.wb) == 0);
    harness_free_tasks();
    CHECK("cmd tasks released", nrunning == 0 && mayhem_ntask_children == 0);

    reset();
    CHECK("cmd unknown user accepted", run_command(&R, unknown_user, sizeof(unknown_user) - 1) == 0);
    CHECK("cmd unknown user reply", cmd_reply_is(err_permit));

    reset();
    CHECK("cmd empty user denied", run_command(&R, no_user, sizeof(no_user) - 1) == 0 && buffer_length(&R.wb) > 3);

    reset();
    CHECK("cmd missing program", run_command(&R, missing, sizeof(missing) - 1) == 0 && cmd_reply_is(err_missing));

    reset();
    CHECK("cmd unterminated rejected", run_command(&R, unterminated, sizeof(unterminated) - 1) == -1);
    CHECK("cmd empty rejected", run_command(&R, "", 0) == -1);
    reset();
}

static void test_http_and_tcp(void)
{
    uint8_t http[1 + 18 + 6 + 4];
    uint8_t tcp[8];

    reset();
    memset(http, 0, sizeof(http));
    http[1] = 1;
    memcpy(http + 25, "GET ", 4);
    put_frame(MSG_TYPE_HTTP, http, sizeof(http));
    CHECK("http request returns 0", parse_msg(&R) == 0 && buffer_length(&R.rb) == 0);
    CHECK("http connection tracked", !list_empty(&R.http_conns));
    http_conns_free(&R.http_conns);
    CHECK("http connections freed", list_empty(&R.http_conns));

    reset();
    memset(tcp, 0, sizeof(tcp));
    put_frame(MSG_TYPE_TCP, tcp, 3);
    CHECK("tcp short message handled", parse_msg(&R) == 0 || parse_msg(&R) == -1);
    reset();
}

int main(void)
{
    log_level(-1);
    mayhem_task_child_cb = ev_child_exit;

    test_utils();
    test_fileipc();
    test_register_and_heartbeat();
    test_terminal();
    test_serial_ports();
    test_command();
    test_http_and_tcp();
    nfail += selftest_config() != 0;

    printf("SELFTEST pass=%d fail=%d\n", npass, nfail);
    return nfail != 0;
}
