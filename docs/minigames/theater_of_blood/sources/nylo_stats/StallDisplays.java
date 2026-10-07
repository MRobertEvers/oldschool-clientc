package com.nylostats;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@Getter
@RequiredArgsConstructor
public enum StallDisplays
{
    OFF("Off"),
    COLLAPSED("Collapsed"),
    ALL("All"),
    ALL_ALIVE("All-Alive"),
    ALL_ALIVE_TOTAL("All-Alive-Total");

    private final String type;

    @Override
    public String toString()
    {
        return type;
    }
}
