# Tests

Current tests are host-side smoke checks.

What they verify now:
- context initialization
- process and thread creation
- ready-queue insertion
- dispatch selection
- equal-priority quantum rotation
- sleep and wake path
- block and explicit unblock path
- process exit state transition

What they do not verify yet:
- real architecture context switch
- interrupt entry/exit
- kernel stack and VM integration
- wait-node proxy semantics
- priority inheritance
- SMP and affinity routing

Run the smoke test with:

```sh
make test
```
