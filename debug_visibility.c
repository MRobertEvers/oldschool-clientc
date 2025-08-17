#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Quick test to understand the visibility calculation
int
main()
{
    // Current condition: abs(cross) < abs(dot) / 2
    // This means tan(θ) < 0.5
    double angle_rad = atan(0.5);
    double angle_deg = angle_rad * 180.0 / M_PI;

    printf("Current FOV threshold analysis:\n");
    printf("tan(θ) < 0.5 means θ < %.2f degrees\n", angle_deg);
    printf("Total field of view: %.2f degrees\n", angle_deg * 2);
    printf("\n");

    // More reasonable FOV would be 60-90 degrees total
    printf("Suggested improvements:\n");
    printf(
        "For 60° total FOV: abs(cross) < abs(dot) * tan(30°) = abs(dot) * %.3f\n",
        tan(30.0 * M_PI / 180.0));
    printf("For 90° total FOV: abs(cross) < abs(dot) * tan(45°) = abs(dot) * 1.0\n");

    return 0;
}
