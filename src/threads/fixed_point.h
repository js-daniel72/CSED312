#include <stdint.h>
#define F (1<<14)

int int_to_fp (int n);
int fp_to_int (int x);
int fp_to_int_round (int x); 
int add_fp (int x, int y); 
int sub_fp (int x, int y); 
int add_fp_int (int x, int n); 
int sub_fp_int (int x, int n); 
int mult_fp (int x, int y); 
int mult_fp_int (int x, int y);
int div_fp (int x, int y); 
int div_fp_int (int x, int n);

int
int_to_fp (int n) {
    return n * F;
}

int
fp_to_int (int x) {
    return x / F;
}

int
fp_to_int_round (int x) {
    if (x >= 0) {
        return (x + F / 2) / F;
    }
    else {
        return (x - F / 2) / F;
    }
}

int
add_fp(int x, int y) {
    return x + y;
}

int
sub_fp(int x, int y){
    return x - y;
}

int
add_fp_int(int x, int n){
    return x + int_to_fp(n);
}

int
sub_fp_int(int x, int n){
    return x - int_to_fp(n);
}

int
mult_fp(int x, int y){
    return ((int64_t)x) * y / F;
}

int
mult_fp_int(int x, int n){
    return x * n;
}

int
div_fp(int x, int y){
    return ((int64_t)x) * F / y;
}

int
div_fp_int(int x, int n){
    return x / n;
}
