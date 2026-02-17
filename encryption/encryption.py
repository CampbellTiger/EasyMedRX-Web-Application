from math import gcd

def key_generate():
    
    p = 113
    q = 293
    n = p * q
    euler = (p-1) * (q-1)
    e = 5
    while(e<euler):
        if (gcd(e, euler) == 1):
            break
        else:
            e += 1

    j = 100
    for i in range(1, j):
        k = i
        d = ((k*euler)+1)/e
        if (int(d)==d):
            d = int(d)
            break
    pub_key = [e, n]
    priv_key = [d,n]
    return pub_key, priv_key

def RSA_Encrypt(message, pub_key):

    plain_ascii = []
    for char in message:
        plain_ascii.append(ord(char))
    plain_encrypt = []
    for num in plain_ascii:
        plain_encrypt.append(chr((num ** pub_key[0]) % pub_key[1]))
    return plain_encrypt


def RSA_Decrypt(message, priv_key):

    decrypted = ''
    for char in message:
        decrypted = decrypted + (str(chr(((ord(char) ** priv_key[0])%priv_key[1]))))
    return decrypted