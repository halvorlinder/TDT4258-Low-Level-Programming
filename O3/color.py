from math import floor
with open('a.txt', 'r') as f:
    for line in f:
        (r,g,b) = line.strip().split(', ')
        print(floor( floor(int(b)/8)*(2**0) + floor(int(g)/4)*(2**5) + floor(int(r)/8)*(2**11) ))
        