#ifndef SECONDARY_H
#define SECONDARY_H

/* Wake and initialize other CPU cores on the SoC. */
void wake_secondary_cores(void);

/* Entry point executed on each secondary core after wake-up. */
void secondary_start(void);

#endif // SECONDARY_H
