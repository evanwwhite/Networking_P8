#include "virtio_net_tests.h"

#include "print.h"
#include "virtio_net.h"

namespace {

constexpr size_t k_selector_buffer_size = 32;
constexpr size_t k_debug_frame_len = 80;
constexpr size_t k_tx_burst_count = VIRTIO_NET_QUEUE_SIZE * 3;
constexpr size_t k_rx_stress_loops = 32;

enum class NetTestCase : uint8_t {
  None,
  Smoke,
  Tx,
  Rx,
  Queue,
  Debug,
  RealTx,
  Unknown,
};

struct TestFrames {
  uint8_t frame_a[60];
  uint8_t frame_b[60];
  uint8_t frame_c[VIRTIO_NET_MAX_FRAME_SIZE];
  uint8_t oversized[VIRTIO_NET_MAX_FRAME_SIZE + 1];
};

TestFrames g_frames{};
bool g_frames_ready = false;

void init_frames() {
  if (g_frames_ready) {
    return;
  }

  for (size_t i = 0; i < sizeof(g_frames.frame_a); ++i) {
    g_frames.frame_a[i] = 0xaa;
  }

  for (size_t i = 0; i < sizeof(g_frames.frame_b); ++i) {
    g_frames.frame_b[i] = uint8_t(i);
  }

  for (size_t i = 0; i < sizeof(g_frames.frame_c); ++i) {
    g_frames.frame_c[i] = uint8_t(i & 0xffU);
  }

  for (size_t i = 0; i < sizeof(g_frames.oversized); ++i) {
    g_frames.oversized[i] = uint8_t((i + 1) & 0xffU);
  }

  g_frames_ready = true;
}

size_t min_size(size_t a, size_t b) { return a < b ? a : b; }

void reset_fake_backend() {
  net_shutdown_backend();
  net_init_fake();
}

bool bytes_equal(const uint8_t *lhs, const uint8_t *rhs, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

bool strings_equal(const char *lhs, const char *rhs) {
  size_t i = 0;
  while (lhs[i] != 0 && rhs[i] != 0) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
    ++i;
  }
  return lhs[i] == rhs[i];
}

void trim_selector(char *buffer) {
  size_t len = 0;
  while (buffer[len] != 0) {
    ++len;
  }

  while (len > 0) {
    const char ch = buffer[len - 1];
    if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') {
      buffer[len - 1] = 0;
      --len;
      continue;
    }
    break;
  }
}

NetTestCase parse_test_case(const char *name) {
  if (strings_equal(name, "smoke")) {
    return NetTestCase::Smoke;
  }
  if (strings_equal(name, "tx")) {
    return NetTestCase::Tx;
  }
  if (strings_equal(name, "rx")) {
    return NetTestCase::Rx;
  }
  if (strings_equal(name, "queue")) {
    return NetTestCase::Queue;
  }
  if (strings_equal(name, "debug")) {
    return NetTestCase::Debug;
  }
  if (strings_equal(name, "real_tx")) {
    return NetTestCase::RealTx;
  }
  return NetTestCase::Unknown;
}

NetTestCase read_test_case(StrongRef<Ext2> fs, char *buffer, size_t buffer_size) {
  auto selector = fs->find(fs->root, "net_test_case");
  if (selector == nullptr || buffer_size == 0) {
    return NetTestCase::None;
  }

  const size_t to_read = min_size(size_t(selector->size_in_bytes()), buffer_size - 1);
  if (to_read == 0) {
    return NetTestCase::None;
  }

  const int64_t count = selector->read_all(0, uint32_t(to_read), buffer);
  if (count <= 0) {
    return NetTestCase::None;
  }

  buffer[count] = 0;
  trim_selector(buffer);
  return parse_test_case(buffer);
}

struct Reporter {
  int failures = 0;

  void pass(const char *name) { KPRINT("*** PASS ?\n", name); }

  void fail(const char *name) {
    ++failures;
    KPRINT("*** FAIL ?\n", name);
  }

  void fail_eq(const char *name, int got, int want) {
    ++failures;
    KPRINT("*** FAIL ? got=? want=?\n", name, Dec(got), Dec(want));
  }

  void fail_bytes(const char *name, size_t len) {
    ++failures;
    KPRINT("*** FAIL ? byte-mismatch len=?\n", name, Dec(len));
  }

  void check(const char *name, bool ok) {
    if (ok) {
      pass(name);
    } else {
      fail(name);
    }
  }

  void check_eq(const char *name, int got, int want) {
    if (got == want) {
      pass(name);
    } else {
      fail_eq(name, got, want);
    }
  }

  void check_bytes(const char *name, const uint8_t *got, const uint8_t *want,
                   size_t len) {
    if (bytes_equal(got, want, len)) {
      pass(name);
    } else {
      fail_bytes(name, len);
    }
  }

  void note(const char *text) { KPRINT("*** NOTE ?\n", text); }

  void summary() { KPRINT("*** SUMMARY failures=?\n", Dec(failures)); }
};

void run_smoke_tests(Reporter &reporter) {
  init_frames();

  uint8_t out[64]{};

  reporter.check("smoke.ready_fake_boot", net_ready());

  net_shutdown_backend();
  reporter.check("smoke.ready_after_shutdown", !net_ready());

  net_init_fake();
  reporter.check("smoke.ready_after_reinit", net_ready());

  reporter.check("smoke.reject_null_tx", !net_send_raw(nullptr, 10));
  reporter.check("smoke.reject_zero_tx", !net_send_raw(g_frames.frame_a, 0));
  reporter.check("smoke.reject_oversized_tx",
                 !net_send_raw(g_frames.oversized,
                               VIRTIO_NET_MAX_FRAME_SIZE + 1));
  reporter.check_eq("smoke.reject_null_rx_buffer", net_recv_raw(nullptr, 64),
                    -1);
  reporter.check_eq("smoke.reject_zero_rx_max", net_recv_raw(out, 0), -1);

  reset_fake_backend();
}

void run_tx_tests(Reporter &reporter) {
  init_frames();
  reset_fake_backend();

  reporter.check("tx.send_one_valid_frame",
                 net_send_raw(g_frames.frame_a, sizeof(g_frames.frame_a)));

  bool multi_send_ok = true;
  for (size_t i = 0; i < 5; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_send_raw(frame, sizeof(g_frames.frame_a))) {
      multi_send_ok = false;
    }
  }
  reporter.check("tx.send_multiple_back_to_back", multi_send_ok);

  reporter.check("tx.send_max_frame",
                 net_send_raw(g_frames.frame_c, VIRTIO_NET_MAX_FRAME_SIZE));

  net_shutdown_backend();
  reporter.check("tx.reject_send_when_backend_disabled",
                 !net_send_raw(g_frames.frame_a, sizeof(g_frames.frame_a)));

  net_init_fake();

  bool burst_ok = true;
  for (size_t i = 0; i < k_tx_burst_count; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_send_raw(frame, sizeof(g_frames.frame_a))) {
      burst_ok = false;
    }
  }
  reporter.check("tx.reclaims_after_many_sends", burst_ok);

  reset_fake_backend();
}

void run_rx_tests(Reporter &reporter) {
  init_frames();
  reset_fake_backend();

  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reporter.check_eq("rx.empty_returns_zero", net_recv_raw(out, sizeof(out)), 0);

  reporter.check("rx.inject_one_frame",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));
  int recv_len = net_recv_raw(out, 128);
  reporter.check_eq("rx.receive_one_frame_len", recv_len,
                    int(sizeof(g_frames.frame_b)));
  if (recv_len == int(sizeof(g_frames.frame_b))) {
    reporter.check_bytes("rx.receive_one_frame_bytes", out, g_frames.frame_b,
                         sizeof(g_frames.frame_b));
  }

  reset_fake_backend();

  reporter.check("rx.inject_exact_size_frame",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  recv_len = net_recv_raw(out, sizeof(g_frames.frame_a));
  reporter.check_eq("rx.receive_exact_size_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("rx.receive_exact_size_bytes", out, g_frames.frame_a,
                         sizeof(g_frames.frame_a));
  }

  reset_fake_backend();

  reporter.check("rx.inject_small_buffer_frame",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  reporter.check_eq("rx.reject_small_output_buffer", net_recv_raw(out, 20), -1);

  reset_fake_backend();

  reporter.check("rx.inject_max_frame",
                 net_fake_inject_rx(g_frames.frame_c, VIRTIO_NET_MAX_FRAME_SIZE));
  recv_len = net_recv_raw(out, VIRTIO_NET_MAX_FRAME_SIZE);
  reporter.check_eq("rx.receive_max_frame_len", recv_len,
                    int(VIRTIO_NET_MAX_FRAME_SIZE));
  if (recv_len == int(VIRTIO_NET_MAX_FRAME_SIZE)) {
    reporter.check_bytes("rx.receive_max_frame_bytes", out, g_frames.frame_c,
                         VIRTIO_NET_MAX_FRAME_SIZE);
  }

  reporter.check("rx.reject_null_inject", !net_fake_inject_rx(nullptr, 10));
  reporter.check("rx.reject_zero_inject",
                 !net_fake_inject_rx(g_frames.frame_a, 0));
  reporter.check("rx.reject_oversized_inject",
                 !net_fake_inject_rx(g_frames.oversized,
                                     VIRTIO_NET_MAX_FRAME_SIZE + 1));

  reset_fake_backend();
}

void run_queue_tests(Reporter &reporter) {
  init_frames();
  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reset_fake_backend();

  reporter.check("queue.recycle_inject_first",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  int recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.recycle_receive_first_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("queue.recycle_receive_first_bytes", out,
                         g_frames.frame_a, sizeof(g_frames.frame_a));
  }

  reporter.check("queue.recycle_inject_second",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));
  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.recycle_receive_second_len", recv_len,
                    int(sizeof(g_frames.frame_b)));
  if (recv_len == int(sizeof(g_frames.frame_b))) {
    reporter.check_bytes("queue.recycle_receive_second_bytes", out,
                         g_frames.frame_b, sizeof(g_frames.frame_b));
  }

  reset_fake_backend();

  reporter.check("queue.order_inject_a",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  reporter.check("queue.order_inject_b",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));

  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.order_receive_a_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("queue.order_receive_a_bytes", out, g_frames.frame_a,
                         sizeof(g_frames.frame_a));
  }

  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.order_receive_b_len", recv_len,
                    int(sizeof(g_frames.frame_b)));
  if (recv_len == int(sizeof(g_frames.frame_b))) {
    reporter.check_bytes("queue.order_receive_b_bytes", out, g_frames.frame_b,
                         sizeof(g_frames.frame_b));
  }

  reporter.check_eq("queue.empty_after_all_consumed", net_recv_raw(out, sizeof(out)),
                    0);

  reset_fake_backend();

  bool fill_ok = true;
  for (size_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_fake_inject_rx(frame, sizeof(g_frames.frame_a))) {
      fill_ok = false;
    }
  }
  reporter.check("queue.fill_ring_to_capacity", fill_ok);
  reporter.check("queue.reject_extra_when_full",
                 !net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));

  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.receive_after_full_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("queue.receive_after_full_bytes", out,
                         g_frames.frame_a, sizeof(g_frames.frame_a));
  }

  reporter.check("queue.recovers_after_full",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));

  reset_fake_backend();

  bool stress_ok = true;
  for (size_t i = 0; i < k_rx_stress_loops; ++i) {
    if (!net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a))) {
      stress_ok = false;
      break;
    }
    recv_len = net_recv_raw(out, sizeof(out));
    if (recv_len != int(sizeof(g_frames.frame_a)) ||
        !bytes_equal(out, g_frames.frame_a, sizeof(g_frames.frame_a))) {
      stress_ok = false;
      break;
    }
  }
  reporter.check("queue.stress_reuse", stress_ok);

  reset_fake_backend();
}

void run_debug_tests(Reporter &reporter) {
  init_frames();
  reset_fake_backend();

  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reporter.check("debug.send_large_frame",
                 net_send_raw(g_frames.frame_c, k_debug_frame_len));
  reporter.check("debug.inject_large_frame",
                 net_fake_inject_rx(g_frames.frame_c, k_debug_frame_len));

  const int recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("debug.receive_large_frame_len", recv_len,
                    int(k_debug_frame_len));
  if (recv_len == int(k_debug_frame_len)) {
    reporter.check_bytes("debug.receive_large_frame_bytes", out,
                         g_frames.frame_c, k_debug_frame_len);
  }

  reporter.note(
      "inspect raw for net: tx enqueue, net: fake tx, net: fake rx inject, "
      "net: rx dequeue, and ... 16 more bytes");

  reset_fake_backend();
}

void run_real_tx_tests(Reporter &reporter) {
  init_frames();

  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reporter.check("real.ready_after_pci_init", net_ready());
  reporter.check("real.fake_inject_unavailable",
                 !net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));

  reporter.check("real.tx_one_valid_frame",
                 net_send_raw(g_frames.frame_a, sizeof(g_frames.frame_a)));

  bool multi_send_ok = true;
  for (size_t i = 0; i < 5; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_send_raw(frame, sizeof(g_frames.frame_a))) {
      multi_send_ok = false;
    }
  }
  reporter.check("real.tx_multiple_back_to_back", multi_send_ok);

  reporter.check("real.tx_max_frame",
                 net_send_raw(g_frames.frame_c, VIRTIO_NET_MAX_FRAME_SIZE));
  reporter.check_eq("real.rx_empty_without_host_backend",
                    net_recv_raw(out, sizeof(out)), 0);
}

} // namespace

void net_run_selected_tests(StrongRef<Ext2> fs) {
  char selector[k_selector_buffer_size]{};
  const NetTestCase test_case = read_test_case(fs, selector, sizeof(selector));

  if (test_case == NetTestCase::None) {
    return;
  }

  Reporter reporter{};

  switch (test_case) {
  case NetTestCase::Smoke:
    run_smoke_tests(reporter);
    break;
  case NetTestCase::Tx:
    run_tx_tests(reporter);
    break;
  case NetTestCase::Rx:
    run_rx_tests(reporter);
    break;
  case NetTestCase::Queue:
    run_queue_tests(reporter);
    break;
  case NetTestCase::Debug:
    run_debug_tests(reporter);
    break;
  case NetTestCase::RealTx:
    run_real_tx_tests(reporter);
    break;
  case NetTestCase::Unknown:
    reporter.fail("selector.unknown_test_case");
    reporter.note(selector);
    break;
  case NetTestCase::None:
    break;
  }

  reporter.summary();
}
