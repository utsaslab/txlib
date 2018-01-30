/*
 * Implemented in fs/hold.c
 */

#ifndef HOLD_H
#define HOLD_H

void hold_page(struct page *);
bool page_is_held(struct page *);

#endif /* HOLD_H */
