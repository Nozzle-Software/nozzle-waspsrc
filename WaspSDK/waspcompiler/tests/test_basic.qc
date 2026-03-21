/*
 * WaspCompiler Test Suite - test_basic.qc
 * Tests core QuakeC language features.
 */

// ============================================================================
// Basic built-in declarations (subset of standard Quake defs.qc)
// ============================================================================

// Entity fields
.float   health;
.float   max_health;
.float   flags;
.float   movetype;
.float   solid;
.vector  origin;
.vector  velocity;
.vector  angles;
.float   nextthink;
.void()  think;
.void()  touch;
.entity  owner;
.string  classname;
.string  model;
.float   modelindex;
.float   frame;
.float   skin;

// Global entities
entity  world;
entity  self;
entity  other;

// Constants
float   FALSE = 0;
float   TRUE  = 1;

float   MOVETYPE_NONE  = 0;
float   MOVETYPE_WALK  = 3;
float   MOVETYPE_FLY   = 9;

float   SOLID_NOT      = 0;
float   SOLID_TRIGGER  = 1;
float   SOLID_BBOX     = 2;

// Built-in functions
void(vector ang) makevectors            = #1;
entity() spawn                          = #14;
void(entity e) remove                   = #15;
void(string s) dprint                   = #25;
string(float f) ftos                    = #26;
void(vector o, vector d, float t, float i) particle = #27;
float(float n) random                   = #7;
void(entity e, float chan, string samp, float vol, float atten) sound = #8;
void(entity e, string m) setmodel       = #3;
void(entity e, vector min, vector max) setsize = #4;
void(entity e, vector o) setorigin      = #2;
float(float n) rint                     = #36;
float(float n) floor                    = #37;
float(float n) ceil                     = #38;
float(float n) fabs                     = #43;
vector(vector v) normalize              = #9;
float(vector v) vlen                    = #12;
string(entity e, float f) gettaginfo    = #451;
void() error                            = #10;
void() objerror                         = #11;
float(entity e) pointcontents           = #16;

// ============================================================================
// Basic math tests
// ============================================================================

float() test_arithmetic =
{
    local float a, b, c;
    a = 10;
    b = 3;
    c = a + b;         // 13
    c = a - b;         // 7
    c = a * b;         // 30
    c = a / b;         // 3.333...
    c = -a;            // -10
    return c;
};

float() test_comparison =
{
    local float a, b;
    a = 5;
    b = 10;

    if (a < b)
        return 1;
    return 0;
};

// ============================================================================
// Control flow tests
// ============================================================================

float() test_while_loop =
{
    local float i, sum;
    i   = 0;
    sum = 0;

    while (i < 10)
    {
        sum = sum + i;
        i = i + 1;
    }
    return sum;   // 45
};

float() test_if_else =
{
    local float x;
    x = 7;
    if (x > 5)
    {
        return 1;
    }
    else
    {
        return 0;
    }
};

void() test_nested_if =
{
    local float a;
    a = 42;
    if (a == 42)
    {
        if (a > 0)
        {
            dprint("positive 42\n");
        }
    }
    else
    {
        dprint("not 42\n");
    }
};

// ============================================================================
// Vector operations
// ============================================================================

vector() test_vector_ops =
{
    local vector v1, v2, result;
    v1 = '1 2 3';
    v2 = '4 5 6';
    result = v1 + v2;     // '5 7 9'
    result = v1 - v2;     // '-3 -3 -3'
    result = result * 2;  // multiply by scalar
    return result;
};

float() test_vector_components =
{
    local vector v;
    v = '3 4 0';
    return vlen(v);    // 5
};

// ============================================================================
// String operations  
// ============================================================================

void() test_strings =
{
    local string s;
    s = "Hello from WaspCompiler!";
    dprint(s);
    dprint("\n");
    dprint(ftos(42));
    dprint("\n");
};

// ============================================================================
// Entity field access
// ============================================================================

void() test_entity_fields =
{
    local entity e;
    e = spawn();
    e.classname = "test_entity";
    e.health    = 100;
    e.movetype  = MOVETYPE_FLY;
    e.solid     = SOLID_BBOX;

    if (e.health > 50)
    {
        dprint("entity has good health\n");
    }

    remove(e);
};

// ============================================================================
// Recursive function
// ============================================================================

float(float n) factorial =
{
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
};

// ============================================================================
// do-while loop
// ============================================================================

float() test_do_while =
{
    local float i, sum;
    i   = 1;
    sum = 0;
    do
    {
        sum = sum + i;
        i   = i + 1;
    } while (i <= 5);
    return sum;   // 15
};

// ============================================================================
// Compound assignment operators
// ============================================================================

float() test_compound_assign =
{
    local float x;
    x  = 10;
    x += 5;     // 15
    x -= 3;     // 12
    x *= 2;     // 24
    x /= 4;     // 6
    return x;
};

// ============================================================================
// Logical operators
// ============================================================================

float() test_logical_ops =
{
    local float a, b;
    a = 1; b = 0;
    if (a && !b)
        return 1;
    return 0;
};

// ============================================================================
// Main / entry point equivalent
// ============================================================================

void() worldspawn =
{
    dprint("WaspCompiler test suite starting...\n");

    // Run tests
    local float result;

    result = test_arithmetic();
    dprint("arithmetic: ");
    dprint(ftos(result));
    dprint("\n");

    result = test_comparison();
    dprint("comparison: ");
    dprint(ftos(result));
    dprint("\n");

    result = test_while_loop();
    dprint("while loop sum 0..9: ");
    dprint(ftos(result));
    dprint("\n");

    result = test_if_else();
    dprint("if/else: ");
    dprint(ftos(result));
    dprint("\n");

    result = test_do_while();
    dprint("do-while 1+2+3+4+5: ");
    dprint(ftos(result));
    dprint("\n");

    result = test_compound_assign();
    dprint("compound assign: ");
    dprint(ftos(result));
    dprint("\n");

    result = test_logical_ops();
    dprint("logical ops: ");
    dprint(ftos(result));
    dprint("\n");

    result = factorial(5);
    dprint("5! = ");
    dprint(ftos(result));
    dprint("\n");

    dprint("WaspCompiler test suite complete.\n");
};
