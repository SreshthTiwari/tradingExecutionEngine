import sys

import yfinance as yf


def get_stock_price(symbol):
    """fetch the latest 1-minute close from yahoo finance"""
    history = yf.Ticker(symbol).history(period="1d", interval="1m")
    if history.empty:
        raise ValueError(f"no data for {symbol}")
    return history["Close"].iloc[-1]


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: python3 yahoo_data.py <symbol>", file=sys.stderr)
        sys.exit(1)

    try:
        print(get_stock_price(sys.argv[1]))
    except Exception as e:
        print(e, file=sys.stderr)
        sys.exit(1)
