# Night queue

An ordered backlog for an unattended run, written 2026-10-06 at the end of a
long session. Each item is scoped to be finishable and gateable on its own, so
stopping between items always leaves the tree in a good state.

## Standing rules for working this list

1. **One item at a time, in order.** Finish it, gate it, commit it, push it,
   then start the next. Do not begin an item you cannot finish.
2. **The gate is the guard.** `./scripts/smoke-test.sh` must pass before any
   commit, and the three build configurations (`make`, `make DOOM=0`,
   `make CHALLENGE=1 DOOM=0`) must all build. A flaky check is a bug in the
   check: fix it rather than re-running until it passes.
3. **Do not guess at ambiguity.** If an item turns out to need a decision that
   is not written here, stop, add a note under that item saying what the
   decision is and what the options are, and move to the next item.
4. **Keep the kitchen vocabulary.** New commands get kitchen names, checked
   against `cmd_names[]` in `shell.c`, and `str_startswith` dispatch entries
   get checked against every existing name for shadowing.
5. **Prove bugs before fixing them.** This session found three latent kernel
   bugs; each one was demonstrated first and the demonstration recorded in the
   commit. Keep doing that.
6. **Stay on `main` in this worktree.** Never touch `cdctf-release`, never push
   anywhere but the Gitea remote, never publish anything to GitHub.
7. **Write the commit message for whoever reads it in a month**, including what
   did *not* work, which is usually the more useful half.

---

## 1. DHCP client

Removes the compiled-in address, which is the last thing stopping soupOS from
working on a network that is not QEMU's.

- SLIRP runs a DHCP server at 10.0.2.2, so this is **hermetic**: it needs no
  real internet and can be asserted in the gate, unlike DNS.
- Scope: DISCOVER, OFFER, REQUEST, ACK. Take the address, netmask, gateway and
  resolver out of the ACK's options. No lease renewal, no rebinding: record the
  lease time and ignore it, with a comment saying so.
- The UDP path already exists (ports 68 -> 67, broadcast to 255.255.255.255
  with a zero source address, which `ip_send` will need to tolerate).
- Shell: fold it into `plumbing` as `plumbing dhcp`, rather than a new command.
- Gate: boot with no compiled-in address and assert the log shows a lease
  being taken and the address matching 10.0.2.15.

## 2. TCP, in three separate commits

The real mountain. Do **not** attempt it in one pass; each stage below should
build, gate and commit on its own.

### 2a. Connection setup

- A single TCB (one connection at a time, like the resolver's single
  outstanding request). SYN, SYN/ACK, ACK, with an initial sequence number and
  the peer's window recorded.
- Retransmit the SYN twice before giving up.
- Nothing above it yet: a `knock` style test that connects and closes is enough
  to gate, asserted against the pcap and a log line.

### 2b. Data transfer

- Send and receive with sequence and acknowledgement numbers, a receive buffer,
  and a retransmit timer driven by the 100 Hz tick.
- No window scaling, no SACK, no congestion control beyond "one segment in
  flight at a time". Say so in the comments; a hobby stack that is honest about
  its limits is more useful than one that pretends.

### 2c. Close, and something to show for it

- FIN/ACK both directions, and the TIME_WAIT that follows.
- Then the payoff: `takeout <host> [path]`, an HTTP GET that prints the
  response. `takeout example.com /` is the demo.
- Gate: assert the request goes out and a 200 comes back. If that needs real
  internet, assert only what is hermetic (the connection to a local port) and
  leave the HTTP check as a manual step, the way the DNS answer is handled.

## 3. Preemption-safety debt

`docs/next-steps.md` has flagged this since before processes existed, and
processes made part of it reachable. Two sites were fixed this session (the
page allocator and the VFS handle table); these remain:

- `soupyc.c`: 69 file-scope statics and a non-reentrant tree walker. Only the
  shell runs it today, so it is not yet reachable, **but** check that claim
  before deciding it is safe, and write down the answer either way.
- `vfs.c` beyond `alloc_node`, `users.c`, `shell.c`: audit what is reachable
  from more than one task now that processes exist, guard what is, and leave a
  comment on what is not and why.
- This is an audit, not a feature: the deliverable is a short written finding
  per file plus guards where needed.

## 4. In-kernel self-test command

The gate is end-to-end only; nothing tests a subsystem directly.

- A shell command (kitchen name: `sample` is free) that runs assertions
  in-kernel and prints pass/fail per subsystem: heap alloc/free patterns, FAT
  round-trips, the VFS handle table, the pipe ring at its boundaries, the ARP
  cache, checksum vectors.
- Make it emit a single machine-readable summary line so the gate can assert on
  it with one check.

---

## Notes left by unattended runs

Append here rather than editing items above: what was finished, what was
stopped on and why, and anything that needs a human decision.
