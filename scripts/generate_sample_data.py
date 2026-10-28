#!/usr/bin/env python3
"""Generate a synthetic order-event file in the engine's text format.

Prices follow a random walk (in integer ticks); orders cluster around the current
price, with a mix of limit/IOC/FOK/market orders, cancels and modifies. The output is
deterministic for a given seed. It is made-up data, not real market history.

usage: generate_sample_data.py [--count N] [--seed S] [--start-price TICKS]
"""
import argparse
import random


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=5000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--start-price", type=int, default=10000)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    price = args.start_price
    next_id = 1
    live = []  # ids that were sent; some will have been filled already, which is realistic

    print(f"# synthetic order flow: seed={args.seed} count={args.count} start={args.start_price}")
    for _ in range(args.count):
        price = max(100, price + rng.choice([-2, -1, 0, 0, 1, 2]))
        roll = rng.random()
        side = rng.choice("BS")

        if roll < 0.12 and live:
            print(f"CANCEL {rng.choice(live)}")
        elif roll < 0.20 and live:
            print(f"MODIFY {rng.choice(live)} {price + rng.randint(-10, 10)} {rng.randint(1, 100)}")
        elif roll < 0.26:
            print(f"MKT {next_id} {side} {rng.randint(1, 60)}")
            next_id += 1
        else:
            tif = rng.choices(["GTC", "IOC", "FOK"], weights=[85, 10, 5])[0]
            qty = rng.randint(1, 100)
            order_price = price + rng.randint(-15, 15)
            print(f"NEW {next_id} {side} {order_price} {qty} {tif}")
            live.append(next_id)
            next_id += 1


if __name__ == "__main__":
    main()
