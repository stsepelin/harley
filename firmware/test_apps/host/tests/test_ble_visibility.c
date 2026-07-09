#include "unity.h"
#include "ble_visibility.h"

// The decision matrix is small enough to enumerate exhaustively. Each
// row of the truth table is its own test so the failure message points
// at the exact case.

static void test_no_bond_no_override_is_undirected(void)
{
    TEST_ASSERT_EQUAL_INT(BLE_ADV_MODE_UNDIRECTED, ble_visibility_decide(false, false));
}

static void test_no_bond_with_override_is_undirected(void)
{
    // Override-on with no bond is the same as no override (nothing to
    // direct to). Confirms the function never returns DIRECTED without
    // a bond — that would crash ble_gap_adv_start since the peer
    // address parameter would be a stale stack value.
    TEST_ASSERT_EQUAL_INT(BLE_ADV_MODE_UNDIRECTED, ble_visibility_decide(false, true));
}

static void test_bond_no_override_is_directed(void)
{
    // Default steady state once the rider has paired one phone.
    TEST_ASSERT_EQUAL_INT(BLE_ADV_MODE_HIDDEN, ble_visibility_decide(true, false));
}

static void test_bond_with_override_is_undirected(void)
{
    // The rider has flipped BT VISIBILITY on to pair a second phone.
    // Bond exists but we advertise to everyone until pairing completes.
    TEST_ASSERT_EQUAL_INT(BLE_ADV_MODE_UNDIRECTED, ble_visibility_decide(true, true));
}

// Auto-revert after a new bond — currently always clears the override,
// but the policy lives in a function so a future "remember last user
// choice" variant can drop in without changing the call site.

static void test_after_new_bond_clears_override_when_was_on(void)
{
    TEST_ASSERT_FALSE(ble_visibility_after_new_bond(true));
}

static void test_after_new_bond_idempotent_when_was_off(void)
{
    TEST_ASSERT_FALSE(ble_visibility_after_new_bond(false));
}

void RunTests(void)
{
    RUN_TEST(test_no_bond_no_override_is_undirected);
    RUN_TEST(test_no_bond_with_override_is_undirected);
    RUN_TEST(test_bond_no_override_is_directed);
    RUN_TEST(test_bond_with_override_is_undirected);
    RUN_TEST(test_after_new_bond_clears_override_when_was_on);
    RUN_TEST(test_after_new_bond_idempotent_when_was_off);
}
