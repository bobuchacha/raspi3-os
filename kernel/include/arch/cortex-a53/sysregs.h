#define HCR_EL2_E2H (1 << 34)
#define HCR_EL2_TGE (1 << 27)
#define HCR_EL2_AMO (1 << 5)    // set this bit to route SError to EL2
#define HCR_EL2_IMO (1 << 4)    // set this bit to route IRQ to EL2
#define HCR_EL2_FMO (1 << 3)    // route FIQ to EL2

#define HCR_EL2_SET_BITS     
#define HCR_EL2_CLEAR_BITS  (HCR_EL2_E2H | HCR_EL2_TGE | HCR_EL2_AMO | HCR_EL2_IMO | HCR_EL2_FMO)