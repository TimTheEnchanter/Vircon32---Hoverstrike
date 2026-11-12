// =============================================================================
//   vector.h - glowing vector graphics for Vircon32
// =============================================================================
//   Lines are stretched, rotated strips of a small texture. Each line is drawn
//   twice: a soft additive halo, then a thin sharp core. Line functions only
//   queue; vec_flush() draws all halos, then all cores.
//
//     vec_init( texture, first_region )     uses 3 regions
//     vec_color( core, glow )               glow 0 = no halo (half the cost)
//     vec_thickness( px )   vec_glow_size( px )
//     vec_line   vec_polyline   vec_shape   vec_circle   vec_arc   vec_rect
//     vec_text   vec_text_center   vec_number   vec_text_width
//     vec_fill_rect   vec_fill_convex       drawn immediately
//     vec_flush()  may be called more than once per frame, for layering
// =============================================================================

#ifndef VECTOR_H
#define VECTOR_H

#include "video.h"
#include "math.h"

#ifndef VEC_MAX_LINES
  #define VEC_MAX_LINES 700
#endif

#define VEC_MAX_POINTS 64

int vec_texture_id = 0;
int vec_region_core = 0;
int vec_region_glow = 1;
int vec_region_pixel = 2;

int vec_current_core = 0xFFFFFFFF;
int vec_current_glow = 0;
float vec_current_thickness = 2.0;
float vec_current_glow_size = 14.0;

float[ VEC_MAX_LINES ] vec_q_x1;
float[ VEC_MAX_LINES ] vec_q_y1;
float[ VEC_MAX_LINES ] vec_q_length;
float[ VEC_MAX_LINES ] vec_q_angle;
float[ VEC_MAX_LINES ] vec_q_thickness;
float[ VEC_MAX_LINES ] vec_q_glow_size;
int[ VEC_MAX_LINES ] vec_q_core;
int[ VEC_MAX_LINES ] vec_q_glow;
int vec_q_count = 0;
float vec_offset_x = 0;      // added to every line at vec_flush (screen shake)
float vec_offset_y = 0;

#ifdef VEC_PEAK
int vec_peak = 0;            // test builds: fullest queue, and lines turned away
int vec_over = 0;
#endif

float[ VEC_MAX_POINTS + 1 ] vec_tmp_x;
float[ VEC_MAX_POINTS + 1 ] vec_tmp_y;

// texture: x = 0, y = 0..3 solid white; x = 2, y = 0..31 gaussian strip
void vec_init( int texture_id, int first_region )
{
    int previous_texture = get_selected_texture();

    vec_texture_id = texture_id;
    vec_region_core = first_region;
    vec_region_glow = first_region + 1;
    vec_region_pixel = first_region + 2;

    select_texture( texture_id );
    select_region( vec_region_core );
    define_region( 0, 0, 0, 3,  0, 2 );
    select_region( vec_region_glow );
    define_region( 2, 0, 2, 31,  2, 16 );
    select_region( vec_region_pixel );
    define_region( 0, 0, 0, 0,  0, 0 );

    select_texture( previous_texture );
    vec_q_count = 0;
}

// display filters: every colour becomes a shade of one phosphor
int vec_filter = 0;         // 0 off, 1 green, 2 amber, 3 white, 4 red

int vec_tint( int c )
{
    int r = c & 255, g = ( c >> 8 ) & 255, b = ( c >> 16 ) & 255;
    int a = ( c >> 24 ) & 255;
    int l = r;

    if( g > l ) l = g;
    if( b > l ) l = b;
    if( vec_filter == 1 ) return l * 70 / 255 | ( l << 8 ) | ( ( l * 100 / 255 ) << 16 ) | ( a << 24 );
    if( vec_filter == 2 ) return l | ( ( l * 176 / 255 ) << 8 ) | ( ( l * 50 / 255 ) << 16 ) | ( a << 24 );
    if( vec_filter == 4 ) return l | ( ( l * 50 / 255 ) << 8 ) | ( ( l * 60 / 255 ) << 16 ) | ( a << 24 );
    return ( l * 235 / 255 ) | ( ( l * 240 / 255 ) << 8 ) | ( l << 16 ) | ( a << 24 );
}

void vec_color( int core_color, int glow_color )
{
    if( vec_filter )
    {
        core_color = vec_tint( core_color );
        if( glow_color )
        {
            // a dimmer shade, or the glow would swallow the line
            glow_color = vec_tint( ( ( glow_color >> 2 ) & 0x3F3F3F ) * 1 | ( glow_color & 0xFF000000 ) );
        }
    }
    vec_current_core = core_color;
    vec_current_glow = glow_color;
}

void vec_thickness( float pixels ) { vec_current_thickness = pixels; }
void vec_glow_size( float pixels ) { vec_current_glow_size = pixels; }
int vec_queued_lines() { return vec_q_count; }

void vec_line( float x1, float y1, float x2, float y2 )
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    int i;

    #ifdef VEC_PEAK
    if( vec_q_count >= VEC_MAX_LINES ) vec_over++;
    #endif
    if( vec_q_count >= VEC_MAX_LINES ) return;

    i = vec_q_count;
    vec_q_x1[ i ] = x1;
    vec_q_y1[ i ] = y1;
    // most lines in text are square to the screen, and these skip two costly
    // instructions. It also keeps us away from atan2( 0, 0 ), a hardware error
    if( dy == 0 )
    {
        if( dx >= 0 ) { vec_q_length[ i ] = dx + 1.0; vec_q_angle[ i ] = 0; }
        else { vec_q_length[ i ] = 1.0 - dx; vec_q_angle[ i ] = 3.1415927; }
    }
    else if( dx == 0 )
    {
        if( dy > 0 ) { vec_q_length[ i ] = dy + 1.0; vec_q_angle[ i ] = 1.5707964; }
        else { vec_q_length[ i ] = 1.0 - dy; vec_q_angle[ i ] = -1.5707964; }
    }
    else
    {
        vec_q_length[ i ] = sqrt( dx*dx + dy*dy ) + 1.0;
        vec_q_angle[ i ] = atan2( dy, dx );
    }
    vec_q_thickness[ i ] = vec_current_thickness;
    vec_q_glow_size[ i ] = vec_current_glow_size;
    vec_q_core[ i ] = vec_current_core;
    vec_q_glow[ i ] = vec_current_glow;
    vec_q_count++;
}

void vec_polyline( float* xs, float* ys, int count, bool closed )
{
    int i;
    for( i = 0; i < count - 1; i++ )
      vec_line( xs[ i ], ys[ i ], xs[ i + 1 ], ys[ i + 1 ] );
    if( closed && count > 2 )
      vec_line( xs[ count - 1 ], ys[ count - 1 ], xs[ 0 ], ys[ 0 ] );
}

// closed shape in model coordinates: rotated, scaled, placed at ( x, y )
void vec_shape( float* xs, float* ys, int count, float x, float y, float angle, float scale )
{
    float c = cos( angle ) * scale;
    float s = sin( angle ) * scale;
    int i;

    if( count > VEC_MAX_POINTS ) count = VEC_MAX_POINTS;
    for( i = 0; i < count; i++ )
    {
        vec_tmp_x[ i ] = x + xs[ i ] * c - ys[ i ] * s;
        vec_tmp_y[ i ] = y + xs[ i ] * s + ys[ i ] * c;
    }
    vec_polyline( vec_tmp_x, vec_tmp_y, count, true );
}

// an ellipse-capable circle: radius_y lets lids and balloons squash
void vec_ellipse( float x, float y, float radius_x, float radius_y, int segments )
{
    float step;
    int i;

    if( segments > VEC_MAX_POINTS ) segments = VEC_MAX_POINTS;
    if( segments < 3 ) segments = 3;
    step = 6.2831853 / segments;
    for( i = 0; i < segments; i++ )
    {
        vec_tmp_x[ i ] = x + radius_x * cos( i * step );
        vec_tmp_y[ i ] = y + radius_y * sin( i * step );
    }
    vec_polyline( vec_tmp_x, vec_tmp_y, segments, true );
}

void vec_circle( float x, float y, float radius, int segments )
{
    vec_ellipse( x, y, radius, radius, segments );
}

void vec_arc( float x, float y, float radius, float from, float to, int segments )
{
    float step;
    int i;

    if( segments > VEC_MAX_POINTS - 1 ) segments = VEC_MAX_POINTS - 1;
    step = ( to - from ) / segments;
    for( i = 0; i <= segments; i++ )
    {
        vec_tmp_x[ i ] = x + radius * cos( from + i * step );
        vec_tmp_y[ i ] = y + radius * sin( from + i * step );
    }
    vec_polyline( vec_tmp_x, vec_tmp_y, segments + 1, false );
}

void vec_rect( float x1, float y1, float x2, float y2 )
{
    vec_line( x1, y1, x2, y1 );
    vec_line( x2, y1, x2, y2 );
    vec_line( x2, y2, x1, y2 );
    vec_line( x1, y2, x1, y1 );
}

// direct port writes: about half the CPU cost of the library calls
void vec_flush()
{
    int i, color;
    float sx, sy, a, px, py;
    int previous_texture = get_selected_texture();

    #ifdef VEC_PEAK
    if( vec_q_count > vec_peak ) vec_peak = vec_q_count;
    #endif

    select_texture( vec_texture_id );
    set_blending_mode( blending_add );
    select_region( vec_region_glow );

    for( i = 0; i < vec_q_count; i++ )
    {
        color = vec_q_glow[ i ];
        if( color )
        {
            sx = vec_q_length[ i ];
            sy = vec_q_glow_size[ i ] / 32.0;
            a = vec_q_angle[ i ];
            px = vec_q_x1[ i ] + vec_offset_x;
            py = vec_q_y1[ i ] + vec_offset_y;
            asm
            {
                "mov R0, {color}"
                "out GPU_MultiplyColor, R0"
                "mov R0, {sx}"
                "out GPU_DrawingScaleX, R0"
                "mov R0, {sy}"
                "out GPU_DrawingScaleY, R0"
                "mov R0, {a}"
                "out GPU_DrawingAngle, R0"
                "mov R0, {px}"
                "cfi R0"
                "out GPU_DrawingPointX, R0"
                "mov R0, {py}"
                "cfi R0"
                "out GPU_DrawingPointY, R0"
                "out GPU_Command, GPUCommand_DrawRegionRotozoomed"
            }
        }
    }

    set_blending_mode( blending_alpha );
    select_region( vec_region_core );

    for( i = 0; i < vec_q_count; i++ )
    {
        color = vec_q_core[ i ];
        sx = vec_q_length[ i ];
        sy = vec_q_thickness[ i ] / 4.0;
        a = vec_q_angle[ i ];
        px = vec_q_x1[ i ] + vec_offset_x;
        py = vec_q_y1[ i ] + vec_offset_y;
        asm
        {
            "mov R0, {color}"
            "out GPU_MultiplyColor, R0"
            "mov R0, {sx}"
            "out GPU_DrawingScaleX, R0"
            "mov R0, {sy}"
            "out GPU_DrawingScaleY, R0"
            "mov R0, {a}"
            "out GPU_DrawingAngle, R0"
            "mov R0, {px}"
            "cfi R0"
            "out GPU_DrawingPointX, R0"
            "mov R0, {py}"
            "cfi R0"
            "out GPU_DrawingPointY, R0"
            "out GPU_Command, GPUCommand_DrawRegionRotozoomed"
        }
    }

    set_multiply_color( color_white );
    set_drawing_scale( 1, 1 );
    set_drawing_angle( 0 );
    select_texture( previous_texture );
    vec_q_count = 0;
}

void vec_fill_rect( float x, float y, float w, float h, int color )
{
    int previous_texture = get_selected_texture();

    if( w <= 0 || h <= 0 ) return;
    if( vec_filter ) color = vec_tint( color );
    select_texture( vec_texture_id );
    select_region( vec_region_pixel );
    set_multiply_color( color );
    set_blending_mode( blending_alpha );
    set_drawing_angle( 0 );
    set_drawing_scale( w, h );
    draw_region_zoomed_at( x, y );
    set_multiply_color( color_white );
    set_drawing_scale( 1, 1 );
    select_texture( previous_texture );
}

// software scanline fill, drawn immediately
void vec_fill_convex( float* xs, float* ys, int count, int color )
{
    float min_y = 100000, max_y = -100000;
    float span_min, span_max, t, xi, ya, yb;
    int y, i, j, y_start, y_end;
    int previous_texture = get_selected_texture();

    for( i = 0; i < count; i++ )
    {
        min_y = fmin( min_y, ys[ i ] );
        max_y = fmax( max_y, ys[ i ] );
    }
    y_start = max( 0, (int)ceil( min_y ) );
    y_end = min( screen_height - 1, (int)floor( max_y ) );

    select_texture( vec_texture_id );
    select_region( vec_region_pixel );
    set_multiply_color( color );
    set_blending_mode( blending_alpha );
    set_drawing_angle( 0 );

    for( y = y_start; y <= y_end; y++ )
    {
        span_min = 100000;
        span_max = -100000;
        for( i = 0; i < count; i++ )
        {
            j = i + 1;
            if( j == count ) j = 0;
            ya = ys[ i ];
            yb = ys[ j ];
            if( ( y >= ya && y < yb ) || ( y >= yb && y < ya ) )
            {
                t = ( y - ya ) / ( yb - ya );
                xi = xs[ i ] + t * ( xs[ j ] - xs[ i ] );
                span_min = fmin( span_min, xi );
                span_max = fmax( span_max, xi );
            }
        }
        if( span_max > span_min )
        {
            set_drawing_scale( span_max - span_min + 1.0, 1.0 );
            draw_region_zoomed_at( span_min, y );
        }
    }
    set_multiply_color( color_white );
    set_drawing_scale( 1, 1 );
    select_texture( previous_texture );
}

// -----------------------------------------------------------------------------
//   VECTOR FONT: cell is 1 wide x 2 tall (times size); segments join a 3x3 grid
//     0 1 2 / 3 4 5 / 6 7 8
// -----------------------------------------------------------------------------

int[ 16 ] vec_seg_a = { 0, 2, 5, 6, 3, 0, 3, 4, 0, 1, 2, 4, 4, 4, 3, 5 };
int[ 16 ] vec_seg_b = { 2, 5, 8, 8, 6, 3, 4, 5, 4, 4, 4, 6, 7, 8, 7, 7 };

float[ 9 ] vec_grid_x = { 0, 0.5, 1, 0, 0.5, 1, 0, 0.5, 1 };
float[ 9 ] vec_grid_y = { 0, 0, 0, 1, 1, 1, 2, 2, 2 };

int[ 36 ] vec_font =
{
    0x0C3F, 0x1200, 0x00DB, 0x008F, 0x00E6, 0x00ED, 0x00FD, 0x0007, 0x00FF, 0x00EF,   // 0-9
    0x00F7, 0x128F, 0x0039, 0x120F, 0x0079, 0x0071, 0x00BD, 0x00F6, 0x1209, 0x001E,   // A-J
    0x2470, 0x0038, 0x0536, 0x2136, 0x003F, 0x00F3, 0x203F, 0x20F3, 0x00ED, 0x1201,   // K-T
    0x003E, 0xC022, 0x2836, 0x2D00, 0x1500, 0x0C09                                    // U-Z
};

void vec_char( int c, float x, float y, float size )
{
    int mask = 0;
    int s, g;
    float ax, ay, bx, by;
    float mx = x + 0.5 * size;

    if( c >= 'a' && c <= 'z' ) c = c - 'a' + 'A';

    // D and B read better as chamfered outlines than as segments
    if( c == 'D' || c == 'B' )
    {
        ax = x + size;
        bx = x + 0.68 * size;
        vec_line( x, y, x, y + 2 * size );
        vec_line( x, y, bx, y );
        vec_line( x, y + 2 * size, bx, y + 2 * size );
        if( c == 'D' )
        {
            vec_line( bx, y, ax, y + 0.35 * size );
            vec_line( ax, y + 0.35 * size, ax, y + 1.65 * size );
            vec_line( ax, y + 1.65 * size, bx, y + 2 * size );
        }
        else
        {
            vec_line( x, y + size, bx, y + size );
            vec_line( bx, y, ax, y + 0.3 * size );
            vec_line( ax, y + 0.3 * size, ax, y + 0.7 * size );
            vec_line( ax, y + 0.7 * size, bx, y + size );
            vec_line( bx, y + size, ax, y + 1.3 * size );
            vec_line( ax, y + 1.3 * size, ax, y + 1.7 * size );
            vec_line( ax, y + 1.7 * size, bx, y + 2 * size );
        }
        return;
    }

    if( c >= '0' && c <= '9' )      mask = vec_font[ c - '0' ];
    else if( c >= 'A' && c <= 'Z' ) mask = vec_font[ c - 'A' + 10 ];
    else if( c == '-' )             mask = 0x00C0;
    else if( c == '+' )             mask = 0x12C0;
    else if( c == '/' )             mask = 0x0C00;
    else if( c == '.' || c == ':' )
    {
        if( c == ':' ) vec_line( mx, y + 0.5 * size, mx, y + 0.6 * size );
        vec_line( mx, y + 1.85 * size, mx, y + 2.0 * size );
        return;
    }
    else if( c == '!' )
    {
        vec_line( mx, y, mx, y + 1.4 * size );
        vec_line( mx, y + 1.8 * size, mx, y + 2.0 * size );
        return;
    }
    else if( c == ',' )
    {
        vec_line( mx, y + 1.75 * size, mx - 0.2 * size, y + 2.3 * size );
        return;
    }
    else if( c == '\'' )
    {
        vec_line( mx, y, mx, y + 0.5 * size );
        return;
    }
    else if( c == '?' )
    {
        mask = 0x0083;
        vec_line( mx, y + size, mx, y + 1.45 * size );
        vec_line( mx, y + 1.8 * size, mx, y + 2.0 * size );
    }

    // text is the dearest thing to draw, so stop as soon as the bits run out:
    // most glyphs use only the low segments
    s = 0;
    while( mask )
    {
        if( mask & 1 )
        {
            g = vec_seg_a[ s ];
            ax = x + vec_grid_x[ g ] * size;
            ay = y + vec_grid_y[ g ] * size;
            g = vec_seg_b[ s ];
            vec_line( ax, ay, x + vec_grid_x[ g ] * size, y + vec_grid_y[ g ] * size );
        }
        mask = mask >> 1;
        s++;
    }
}

float vec_text_width( int* text, float size )
{
    int n = 0;
    while( text[ n ] ) n++;
    if( n == 0 ) return 0;
    return ( n * 1.6 - 0.6 ) * size;
}

void vec_text( int* text, float x, float y, float size )
{
    while( *text )
    {
        vec_char( *text, x, y, size );
        x += 1.6 * size;
        text++;
    }
}

void vec_text_center( int* text, float center_x, float y, float size )
{
    vec_text( text, center_x - vec_text_width( text, size ) / 2, y, size );
}

void vec_number( int value, float x, float y, float size )
{
    int[ 12 ] digits;
    int n = 0, i;

    if( value < 0 )
    {
        vec_char( '-', x, y, size );
        x += 1.6 * size;
        value = -value;
    }
    do
    {
        digits[ n ] = '0' + value % 10;
        value /= 10;
        n++;
    }
    while( value > 0 );

    for( i = n - 1; i >= 0; i-- )
    {
        vec_char( digits[ i ], x, y, size );
        x += 1.6 * size;
    }
}

// width of vec_number's output
float vec_number_width( int value, float size )
{
    int n = 1;
    while( value >= 10 ) { value /= 10; n++; }
    return ( n * 1.6 - 0.6 ) * size;
}

#endif
