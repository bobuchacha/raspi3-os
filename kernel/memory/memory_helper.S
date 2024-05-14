.global mair_el2_store
mair_el2_store:
  msr mair_el2, x0
  ret

.global mair_el2_load
mair_el2_load:
  mrs x0, mair_el2
  ret