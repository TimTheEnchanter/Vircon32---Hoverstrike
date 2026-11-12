// =============================================================================
//   HOVERSTRIKE  -  a saucer-versus-tanks vector shooter for the Vircon32
//
//   Rules follow the 1983 Atari 2600 saucer-versus-tanks cartridge as
//   documented in Dennis Debro's commented disassembly: the fixed saucer and
//   scrolling landscape, the four tank rows, the downward-sweeping laser, the
//   single tank missile, the shrinking hover limit, the rack scores
//   (53/42/31/20) and the rack-by-rack speed pattern.
//   Code, shapes, sounds and names are original; vector.h is the project's
//   shared glowing-vector library.
//
//   Internally the game uses the cartridge's units: x 0..80 and y 0..74
//   (two-line kernel rows), scaled onto the 640 x 360 screen.
// =============================================================================

#define VEC_MAX_LINES 700

#include "video.h"
#include "input.h"
#include "audio.h"
#include "time.h"
#include "math.h"
#include "string.h"
#include "misc.h"
#include "vector.h"

// -----------------------------------------------------------------------------
//   SOUNDS (order matches tools/make_assets.py and the ROM definition)
// -----------------------------------------------------------------------------

#define SND_LASER      0
#define SND_TANK_BOOM  1
#define SND_MISSILE    2
#define SND_HIT        3
#define SND_CRASH      4
#define SND_RACK_CLEAR 5
#define SND_HUM        6
#define SND_BLIP       7

#define CH_HUM  0
#define CH_FX0  1
#define NUM_FX  6

// -----------------------------------------------------------------------------
//   RULES (from the disassembly)
// -----------------------------------------------------------------------------

#define XMIN             1
#define XMAX             72
#define SAUCER_X         56        // INIT_SAUCER_HORIZ_POS (the saucer never moves sideways)
#define LASER_X          60        // saucer x + 4
#define SAUCER_TOP       16        // INIT_SAUCER_VERT_POS
#define SAUCER_GROUND    40        // INIT_SAUCER_VERT_POS + 24
#define HIT_TIME         31        // INIT_OBJECT_HIT_TIMER (in logic ticks)
#define SCROLL_SPEED     2         // TANK_SCROLL_SPEED

int[ 4 ] tank_row = { 48, 54, 60, 67 };          // VERT_POSITION_TANK_00..03
int[ 4 ] laser_reach = { 53, 59, 66, 69 };       // SaucerLaserScanlineValues
// TankFrameDelayValues by game speed: a tank steps on ticks where (tick & mask) == 0.
// Speeds 5 and 6 read past the table into other data in the cartridge; 1 is used here.
int[ 7 ] tank_delay = { 0x43, 0x43, 0x03, 0x23, 0x21, 0x01, 0x01 };

#define T_OFF   0      // circling unseen until it rolls in from the right
#define T_LIVE  1
#define T_BOOM  2
#define T_WRECK 3

// -----------------------------------------------------------------------------
//   STATE
// -----------------------------------------------------------------------------

#define GS_TITLE    0
#define GS_PLAY     1
#define GS_LANDED   2
#define GS_GAMEOVER 3

int game_state;
int state_timer;
int frame;
int tick;              // logic ticks run every other frame, like the cartridge
bool paused;
int game_select;       // 1..4: the starting speed
int speed;             // currentGameSpeed
int rack;
int score;
int high_score;

float saucer_y;
int hover_limit;
int scroll_dir;        // 0 none, 1 world moves right (flying left), -1 world moves left
float world;           // landscape scroll position, for the mountains

int[ 4 ] tank_state;
float[ 4 ] tank_x;
int remaining;
int active_tank;

int laser_stage;       // 0 = off, 1..4 = beam tip at that tank row
bool fire_held;
int hit_timer;         // ticks
bool saucer_hit;       // the hit timer belongs to a missile strike

bool missile_on;
float missile_x, missile_y;

int fx_ch;
int fx_seed;

// -----------------------------------------------------------------------------
//   HELPERS
// -----------------------------------------------------------------------------

float sx( float x ) { return 20 + x * 7.5; }
float sy( float y ) { return 30 + y * 4.4; }

float fx_range( float a, float b )
{
    fx_seed = fx_seed * 1103515245 + 12345;
    int v = (fx_seed >> 8) & 32767;
    return a + (b - a) * v / 32767.0;
}

void sfx( int sound )
{
    int ch = CH_FX0 + fx_ch;
    fx_ch = (fx_ch + 1) % NUM_FX;
    stop_channel( ch );
    play_sound_in_channel( sound, ch );
}

// -----------------------------------------------------------------------------
//   RACKS
// -----------------------------------------------------------------------------

void reset_tanks()
{
    // all four start out of sight and roll in from the right edge
    for( int i = 0; i < 4; ++i )
    {
        tank_state[i] = T_OFF;
        tank_x[i] = XMAX + i * 8;
    }

    remaining = 4;
}

void next_rack()
{
    // $0FB0: racks 4-7 and 12-15 run one step faster, rack 8 two steps
    rack += 1;
    speed = game_select;
    if( (rack & 15) == 8 ) speed += 2;
    else if( rack & 4 ) speed += 1;
    reset_tanks();
    sfx( SND_RACK_CLEAR );
}

void start_game()
{
    score = 0;
    rack = 0;
    speed = game_select;
    saucer_y = SAUCER_TOP;
    hover_limit = SAUCER_TOP;
    scroll_dir = 0;
    laser_stage = 0;
    hit_timer = 0;
    saucer_hit = false;
    missile_on = false;
    active_tank = 3;
    tick = 0;
    paused = false;
    reset_tanks();
    game_state = GS_PLAY;
}

// -----------------------------------------------------------------------------
//   TANKS
// -----------------------------------------------------------------------------

float tank_velocity( int i )
{
    // $0869: one tank (picked by the score's last two digits) is quicker;
    // with an odd score they all run left, one step faster
    int ones = score % 100;
    int hundreds = score / 100;
    int v = 1;

    if( (ones & 3) == i )
    {
        v = 2;
        if( hundreds & 1 ) v = 3;
    }

    if( ones & 1 ) v = -v - 1;
    return v;
}

void update_tanks()
{
    // movement is spread across the two frames of each logic tick
    for( int i = 0; i < 4; ++i )
    {
        if( tank_state[i] == T_OFF && tank_x[i] >= XMAX && scroll_dir >= 0 ) tank_state[i] = T_LIVE;

        tank_x[i] += scroll_dir * SCROLL_SPEED * 0.5;

        if( (tick & tank_delay[ speed ]) == 0 ) tank_x[i] += tank_velocity( i ) * 0.5;

        if( tank_x[i] >= XMAX + 3 ) tank_x[i] = XMIN;
        if( tank_x[i] < XMIN ) tank_x[i] = XMAX;

        // an explosion turns into a wreck once the hit animation ends
        if( tank_state[i] == T_BOOM && hit_timer == 0 ) tank_state[i] = T_WRECK;
    }

    // the rack is done when all four are wrecked
    if( remaining == 0 && hit_timer == 0 ) next_rack();
}

void update_missile( bool tick_frame )
{
    if( !missile_on || missile_x >= XMAX - 8 || missile_y < 19 )
    {
        missile_on = false;
        if( !tick_frame ) return;

        // a new missile only in alternate 64-tick spells, from the next tank in turn
        if( (tick & 0x40) == 0 ) return;
        active_tank = (active_tank + 1) % 4;
        if( tank_state[ active_tank ] != T_LIVE ) return;

        missile_on = true;
        missile_x = tank_x[ active_tank ] + 4;
        missile_y = tank_row[ active_tank ];
        sfx( SND_MISSILE );
    }

    // up and to the right at the game speed, plus the landscape scroll
    missile_y -= speed * 0.5;
    missile_x += speed * 0.5 + scroll_dir * SCROLL_SPEED * 0.5;
}

// -----------------------------------------------------------------------------
//   SAUCER
// -----------------------------------------------------------------------------

void update_saucer( bool tick_frame )
{
    select_gamepad( 0 );

    if( gamepad_up() > 0 && saucer_y > hover_limit ) saucer_y -= 0.5;
    if( gamepad_down() > 0 && saucer_y < SAUCER_GROUND ) saucer_y += 0.5;
    if( saucer_y < hover_limit ) saucer_y = hover_limit;

    // the landscape scrolls while Left or Right is held ($1150 clears it every frame)
    scroll_dir = 0;
    if( gamepad_left() > 0 ) scroll_dir = 1;
    if( gamepad_right() > 0 ) scroll_dir = -1;

    bool fire = gamepad_button_a() > 0 || gamepad_button_b() > 0;

    // the laser sweeps down one tank row per tick; no firing during a hit
    if( hit_timer > 0 ) laser_stage = 0;
    else if( fire && !fire_held && laser_stage == 0 )
    {
        laser_stage = 1;
        sfx( SND_LASER );
    }
    else if( tick_frame && laser_stage > 0 )
    {
        laser_stage += 1;
        if( laser_stage > 4 ) laser_stage = 0;
    }

    fire_held = fire;
}

void check_laser()
{
    if( laser_stage == 0 ) return;
    int i = laser_stage - 1;
    if( tank_state[i] != T_LIVE ) return;

    // the beam hits the tank in the row it has reached, if it's underneath
    if( tank_x[i] > LASER_X - 8 && tank_x[i] <= LASER_X )
    {
        tank_state[i] = T_BOOM;
        hit_timer = HIT_TIME;
        saucer_hit = false;
        remaining -= 1;

        // $1085: 53, 42, 31, 20 for the first to last tank of a rack
        score += (remaining + 2) * 10 + remaining;
        if( score > 9999 ) score = 9999;
        if( score > high_score ) high_score = score;

        laser_stage = 0;
        sfx( SND_TANK_BOOM );
    }
}

void check_missile_hit()
{
    if( !missile_on ) return;
    float dy = missile_y - saucer_y;
    float dx = missile_x - SAUCER_X;

    if( dy >= 0 && dy < 4 && dx >= 0 && dx < 8 )
    {
        missile_on = false;
        hit_timer = HIT_TIME;
        saucer_hit = true;
        sfx( SND_HIT );
    }
}

void update_hit_timer( bool tick_frame )
{
    if( hit_timer == 0 || !tick_frame ) return;
    hit_timer -= 1;
    if( hit_timer > 0 || !saucer_hit ) return;

    // each strike lowers the hover limit and pushes the saucer down;
    // once it's pushed to the ground the game is over
    saucer_hit = false;
    hover_limit += 4;
    saucer_y += 4;

    if( saucer_y >= SAUCER_GROUND )
    {
        saucer_y = SAUCER_GROUND;
        game_state = GS_LANDED;
        state_timer = 0;
        stop_channel( CH_HUM );
        sfx( SND_CRASH );
    }
}

// -----------------------------------------------------------------------------
//   DRAWING
// -----------------------------------------------------------------------------

float ridge( float u )
{
    // a repeating jagged mountain line
    return 4.5 * sin( u * 0.21 ) + 2.5 * sin( u * 0.57 + 1.3 ) + 1.2 * sin( u * 1.31 + 0.4 );
}

void draw_landscape()
{
    // mountains along the horizon, scrolling with the ground at half speed
    vec_thickness( 2 );
    vec_glow_size( 12 );
    vec_color( 0xFF5050E0, 0xFF201050 );
    float px = 0, py = 0;

    for( int k = 0; k <= 32; ++k )
    {
        float x = k * 2.5;
        float y = 40 + ridge( x - world * 0.5 );
        if( k > 0 ) vec_line( sx( px ), sy( py ), sx( x ), sy( y ) );
        px = x;
        py = y;
    }

    // the ground: the horizon line and four lanes
    vec_color( 0xFF3080C0, 0xFF102040 );
    vec_line( 0, sy( 45 ), screen_width, sy( 45 ) );
    vec_color( 0xFF285070, 0 );

    for( int i = 0; i < 4; ++i )
      vec_line( 0, sy( tank_row[i] + 4.5 ), screen_width, sy( tank_row[i] + 4.5 ) );

    // ground marks slide with the scroll, so you can see you're moving
    for( int k = 0; k < 10; ++k )
    {
        float x = k * 9 + world;
        while( x >= 82 ) x -= 90;
        while( x < -8 ) x += 90;
        vec_line( sx( x ), sy( 72 ), sx( x - 3 ), sy( 74 ) );
    }
}

void draw_tank( int i )
{
    float x = sx( tank_x[i] + 4 );
    float y = sy( tank_row[i] );
    int st = tank_state[i];
    if( st == T_OFF ) return;

    if( st == T_BOOM )
    {
        vec_color( 0xFF40C0FF, 0xFF104060 );
        for( int k = 0; k < 7; ++k )
        {
            float a = k * 0.9 + hit_timer * 0.3;
            float r = 8 + (HIT_TIME - hit_timer) * 0.8;
            vec_line( x + cos( a ) * r * 0.4, y + sin( a ) * r * 0.25, x + cos( a ) * r, y + sin( a ) * r * 0.6 );
        }
        return;
    }

    if( st == T_WRECK )
    {
        // a flattened, smoking hulk
        vec_color( 0xFF606060, 0 );
        vec_line( x - 22, y + 16, x + 22, y + 16 );
        vec_line( x - 18, y + 16, x - 10, y + 10 );
        vec_line( x - 10, y + 10, x + 14, y + 12 );
        vec_line( x + 14, y + 12, x + 20, y + 16 );
        float s = (frame + i * 17) % 40;
        vec_line( x + 2, y + 6 - s * 0.5, x + 4, y + 2 - s * 0.5 );
        return;
    }

    // treads, a sloped hull, a turret and a barrel raised at the sky
    float d = 1;
    if( tank_velocity( i ) < 0 ) d = -1;
    vec_color( 0xFF60F0A0, 0xFF205030 );
    vec_rect( x - 22, y + 10, x + 22, y + 17 );
    vec_line( x - 18, y + 10, x - 14, y + 3 );
    vec_line( x - 14, y + 3, x + 14, y + 3 );
    vec_line( x + 14, y + 3, x + 18, y + 10 );
    vec_rect( x - 7, y - 3, x + 7, y + 3 );
    vec_line( x + d * 7, y - 2, x + d * 18, y - 10 );

    // the treads roll
    float roll = (frame / 4) % 4;
    for( int k = 0; k < 5; ++k ) vec_line( x - 18 + k * 9 + roll * d, y + 11, x - 18 + k * 9 + roll * d, y + 16 );
}

void draw_saucer( float y_units, bool flashing )
{
    float x = sx( LASER_X );
    float y = sy( y_units + 2 );
    int c = 0xFFFFD070;
    int g = 0xFF604010;
    if( flashing && (frame & 4) ) { c = 0xFF4040FF; g = 0xFF101060; }
    vec_color( c, g );

    // a flat disc with a glass dome and a ring of lights
    vec_ellipse( x, y + 3, 30, 7, 18 );
    vec_arc( x, y + 1, 12, 3.1416, 6.2832, 10 );
    vec_line( x - 12, y + 1, x + 12, y + 1 );

    for( int k = 0; k < 5; ++k )
    {
        float a = k * 1.2566 + frame * 0.06;
        float lx = x + cos( a ) * 22;
        if( sin( a ) > 0 ) vec_line( lx, y + 5, lx, y + 7 );
    }
}

void draw_world()
{
    clear_screen( color_black );

    // the sky flashes during hits
    if( hit_timer > 0 && (hit_timer & 2) )
    {
        if( saucer_hit ) vec_fill_rect( 0, 0, screen_width, sy( 45 ), 0xFF301040 );
        else vec_fill_rect( 0, 0, screen_width, sy( 45 ), 0xFF202010 );
    }

    draw_landscape();

    vec_thickness( 2 );
    vec_glow_size( 12 );
    for( int i = 0; i < 4; ++i ) draw_tank( i );

    // the laser: a beam down to the row it has reached
    if( laser_stage > 0 && game_state == GS_PLAY )
    {
        vec_thickness( 3 );
        vec_glow_size( 16 );
        vec_color( 0xFFFFFFFF, 0xFFFF8040 );
        vec_line( sx( LASER_X ), sy( saucer_y + 4 ), sx( LASER_X ), sy( laser_reach[ laser_stage - 1 ] ) );
    }

    if( missile_on )
    {
        vec_thickness( 2 );
        vec_glow_size( 12 );
        vec_color( 0xFF80FFFF, 0xFF206060 );
        float mx = sx( missile_x ), my = sy( missile_y );
        vec_line( mx - 3, my + 4, mx + 3, my - 4 );
        vec_line( mx - 1, my + 5, mx + 4, my - 2 );
    }

    vec_thickness( 2 );
    vec_glow_size( 14 );
    if( game_state == GS_PLAY ) draw_saucer( saucer_y, hit_timer > 0 && saucer_hit );
    vec_flush();
}

void draw_hud()
{
    vec_thickness( 2 );
    vec_color( 0xFFE0E0E0, 0 );
    vec_number( score, 14, 8, 7 );

    vec_color( 0xFF808080, 0 );
    vec_text( "RACK", 250, 10, 5 );
    vec_number( rack + 1, 300, 10, 5 );
    vec_text( "HI", 520, 10, 5 );
    vec_number( high_score, 545, 10, 5 );

    // one notch per strike the saucer can take from its current height
    // (each strike pushes it 4 lower; reaching the ground ends the game)
    int left = (int)((SAUCER_GROUND - saucer_y + 3.99) / 4);
    if( left < 0 ) left = 0;
    for( int k = 0; k < left; ++k )
    {
        vec_color( 0xFFFFD070, 0 );
        vec_rect( 120 + k * 14, 12, 128 + k * 14, 20 );
    }

    if( paused )
    {
        vec_color( 0xFFFFFFFF, 0xFF404040 );
        vec_text_center( "PAUSED", screen_width / 2, 150, 14 );
    }

    vec_flush();
}

// -----------------------------------------------------------------------------
//   GAME STATES
// -----------------------------------------------------------------------------

void run_play()
{
    select_gamepad( 0 );

    if( gamepad_button_start() == 1 )
    {
        paused = !paused;
        if( paused ) pause_all_channels();
        else resume_all_channels();
    }

    if( !paused )
    {
        frame += 1;
        bool tick_frame = (frame & 1) == 0;
        if( tick_frame ) tick += 1;

        world += scroll_dir * SCROLL_SPEED * 0.5;
        update_saucer( tick_frame );
        update_tanks();
        update_missile( tick_frame );
        check_laser();
        check_missile_hit();
        update_hit_timer( tick_frame );

        if( game_state == GS_PLAY && get_channel_state( CH_HUM ) != channel_playing )
        {
            stop_channel( CH_HUM );
            play_sound_in_channel( SND_HUM, CH_HUM );
        }
    }

    draw_world();
    draw_hud();
}

void run_landed()
{
    // the saucer drops onto the ground and smoulders
    state_timer += 1;
    frame += 1;
    clear_screen( color_black );
    draw_landscape();
    vec_thickness( 2 );
    vec_glow_size( 12 );
    for( int i = 0; i < 4; ++i ) draw_tank( i );

    float y = SAUCER_GROUND + state_timer * 0.05;
    if( y > 43 ) y = 43;
    draw_saucer( y, true );

    vec_color( 0xFF808080, 0 );
    for( int k = 0; k < 4; ++k )
    {
        float s = (state_timer * 0.7 + k * 13) - ((int)((state_timer * 0.7 + k * 13) / 50)) * 50;
        float x = sx( LASER_X ) - 12 + k * 8 + fx_range( -1, 1 );
        vec_line( x, sy( y ) - s, x + 2, sy( y ) - s - 4 );
    }

    vec_flush();
    draw_hud();

    if( state_timer > 150 )
    {
        game_state = GS_GAMEOVER;
        state_timer = 0;
    }
}

void run_gameover()
{
    state_timer += 1;
    frame += 1;
    clear_screen( color_black );
    draw_landscape();
    vec_flush();
    draw_hud();

    vec_thickness( 3 );
    vec_color( 0xFF6060FF, 0xFF202060 );
    vec_text_center( "GROUNDED", screen_width / 2, 130, 16 );
    vec_thickness( 2 );
    vec_color( 0xFFE0E0E0, 0 );
    vec_text_center( "SCORE", screen_width / 2 - 40, 200, 7 );
    vec_number( score, screen_width / 2 + 30, 200, 7 );
    vec_flush();

    select_gamepad( 0 );
    if( state_timer > 90 && (gamepad_button_start() == 1 || gamepad_button_a() == 1) )
    {
        game_state = GS_TITLE;
        state_timer = 0;
    }
}

void run_title()
{
    select_gamepad( 0 );
    state_timer += 1;
    frame += 1;
    world += 0.4;

    if( gamepad_left() == 1 )  { game_select = (game_select + 2) % 4 + 1; sfx( SND_BLIP ); }
    if( gamepad_right() == 1 ) { game_select = game_select % 4 + 1; sfx( SND_BLIP ); }

    if( (gamepad_button_start() == 1 || gamepad_button_a() == 1) && state_timer > 20 )
    {
        start_game();
        return;
    }

    clear_screen( color_black );
    draw_landscape();
    vec_flush();

    vec_thickness( 3 );
    vec_glow_size( 18 );
    vec_color( 0xFFFFD070, 0xFF805020 );
    vec_text_center( "HOVERSTRIKE", screen_width / 2, 40, 18 );
    vec_thickness( 2 );
    vec_glow_size( 14 );
    draw_saucer( 22 + sin( frame * 0.05 ) * 1.5, false );
    vec_flush();

    vec_thickness( 2 );
    vec_color( 0xFFFFFFFF, 0xFF404040 );
    vec_text_center( "GAME", screen_width / 2 - 20, 260, 7 );
    vec_number( game_select, screen_width / 2 + 34, 260, 7 );
    vec_color( 0xFF808080, 0 );
    vec_text_center( "LEFT RIGHT CHOOSE THE STARTING SPEED", screen_width / 2, 290, 4 );

    if( (state_timer / 30) % 2 == 0 )
    {
        vec_color( 0xFFFFD070, 0 );
        vec_text_center( "PRESS START", screen_width / 2, 320, 6 );
    }

    if( high_score > 0 )
    {
        vec_color( 0xFF808080, 0 );
        vec_text( "HI", 520, 10, 5 );
        vec_number( high_score, 545, 10, 5 );
    }

    vec_flush();
}

// -----------------------------------------------------------------------------
//   MAIN
// -----------------------------------------------------------------------------

void main( void )
{
    vec_init( 0, 0 );
    set_global_volume( 0.6 );
    select_sound( SND_HUM );
    set_sound_loop( true );
    fx_seed = 99;
    game_select = 1;
    game_state = GS_TITLE;

    while( true )
    {
        if( game_state == GS_TITLE )         run_title();
        else if( game_state == GS_PLAY )     run_play();
        else if( game_state == GS_LANDED )   run_landed();
        else if( game_state == GS_GAMEOVER ) run_gameover();

        end_frame();
    }
}
