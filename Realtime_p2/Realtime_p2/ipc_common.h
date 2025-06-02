/*============================================================
 * ipc_common.h  –  global definitions shared by every process
 *===========================================================*/
#ifndef IPC_COMMON_H
#define IPC_COMMON_H

#include <time.h>
#include <sys/types.h>

/* ---------- product codes sent via message queue ---------- */
enum {
    PRODUCT_PASTE         = 1,
    PRODUCT_CAKE_BATTER   = 2,
    PRODUCT_SANDWICH_FILL = 3,
    PRODUCT_SWEET_MIX     = 4,
    PRODUCT_PAT_SWEET     = 5,
    PRODUCT_PAT_SAVORY    = 6
};

/* ---------- message structure ----------------------------- */
typedef struct {
    long  mtype;          /* PRODUCT_* value                */
    char  text[16];       /* printable name (“cake”, …)      */
} message_t;

#define MAX_CUSTOMERS 64
typedef struct {
    pid_t pid;          /* 0 == slot unused                     */
    int   status;       /* see codes above                      */
    time_t arrived;     /* epoch seconds                        */
    time_t left;        /* epoch seconds (if status!=0)         */
    int   code;
} customer_t;

/*  payload size helper (for msgsnd / msgrcv) */
#define MSG_PAYLOAD  (sizeof(message_t) - sizeof(long))

/* ---------- shared memory layout -------------------------- */
#define MOVE_LOG_SZ  32          /* ring-buffer for reassignment events */

typedef struct {

    /* ── 1. Ingredients & intermediates ──────────────────── */
    int wheat, yeast, butter, milk, sugar, salt;
    int cheese, salami, sweet_items;
    int paste;                          /* intermediate */

    /* ── 2. Finished products in display counter ─────────── */
    int bread,        bad_bread;
    int sandwiches,   bad_sandwiches;
    int cakes,        bad_cakes;
    int sweets,       bad_sweets;
    int patis_sweet,  bad_patis_sweet;
    int patis_savory, bad_patis_savory;

    /* ── 3. Items currently baking (“in oven”) ───────────── */
    int oven_bread;
    int oven_sandwiches;
    int oven_cakes;
    int oven_sweets;
    int oven_patis_sweet;
    int oven_patis_savory;

    /* ── 4. Global money & customer counters ─────────────── */
    int profit;                     /* in cents  */

    int missing_item_requests;

    /* ── 5. Live team counts ─────────────────────────────── */
    /* chefs */
    int c_paste, c_cake, c_sand, c_sweet, c_pat_s, c_pat_v;
    /* bakers */
    int b_bread, b_cs, b_pat;

    /* ── 6. Last MOVE_LOG_SZ team-switch events (optional) ─ */
    struct { time_t ts; int pid; char from[8], to[8]; } moves[MOVE_LOG_SZ];
    int moves_head;                 /* write index in moves[] */

    /* ── 7. Misc. runtime info ───────────────────────────── */
    time_t simulation_start;

    /* ── 8. Supply-purchase ranges (min / max) ───────────── */
    int purchase_wheat_min,  purchase_wheat_max;
    int purchase_yeast_min,  purchase_yeast_max;
    int purchase_butter_min, purchase_butter_max;
    int purchase_milk_min,   purchase_milk_max;
    int purchase_sugar_min,  purchase_sugar_max;
    int purchase_salt_min,   purchase_salt_max;
    int purchase_sweet_items_min, purchase_sweet_items_max;
    int purchase_cheese_min, purchase_cheese_max;
    int purchase_salami_min, purchase_salami_max;

    customer_t customers[MAX_CUSTOMERS];
    int customers_in_store;      /* current # of customers in the bakery      */
    int waiting_customers;       /* currently queued / not yet served          */
    int served_customers;        /* completed purchases                        */
    int complaining_customers;
    int total_customers;
    /* already added earlier: */
    int frustrated_customers;    /* (existing field)                           */
    int max_customer_wait_ms;    /* timeout threshold loaded from config       */


} shm_state_t;

#endif /* IPC_COMMON_H */
