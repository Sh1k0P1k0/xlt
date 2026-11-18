/* XLT - the order of the home menu's cards, as the user arranged it.
 *
 * Pure: the caller names the cards (keys[i] for card i, e.g. "games") and
 * keeps the order as an array of card numbers. xlt.ini stores it as the keys
 * joined by commas ("home_order=games,homebrew,music,..."). A saved order
 * that names a card no longer there is read without it; a card it doesn't
 * name (one added in a later version) goes at the end, in the default order.
 */
#ifndef HOMEORDER_H
#define HOMEORDER_H

/* Fills order[0..n-1] with the cards 0..n-1, each once: first the ones text
 * names (in its order, case ignored, repeats and unknown names skipped), then
 * the rest by number. text may be NULL or "" (the default order). */
void homeorder_parse(const char *text, const char *const *keys, int n, int *order);

/* Writes the keys of order[0..n-1] joined by commas. Returns the length, or
 * -1 if it didn't fit in cap (out is then ""). */
int homeorder_format(const int *order, const char *const *keys, int n, char *out, int cap);

/* Swaps the card at position pos with its neighbour in direction dir (-1/+1).
 * Returns the card's new position, or -1 if it was already at that end. */
int homeorder_move(int *order, int n, int pos, int dir);

#endif
