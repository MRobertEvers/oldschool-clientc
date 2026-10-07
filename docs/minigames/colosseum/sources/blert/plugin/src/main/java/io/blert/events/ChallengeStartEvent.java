/*
 * Copyright (c) 2024 Alexei Frolov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the “Software”), to deal in
 * the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

package io.blert.events;

import io.blert.core.Challenge;
import io.blert.core.ChallengeMode;
import io.blert.core.Stage;
import java.util.List;
import javax.annotation.Nullable;
import lombok.Getter;

@Getter
public class ChallengeStartEvent extends Event {
    private final Challenge challenge;
    private final ChallengeMode mode;
    private final List<String> party;
    private final boolean isSpectator;

    public ChallengeStartEvent(
            Challenge challenge, ChallengeMode mode, @Nullable Stage stage, List<String> party, boolean isSpectator) {
        super(EventType.CHALLENGE_START, stage, 0, null);
        this.challenge = challenge;
        this.mode = mode;
        this.party = party;
        this.isSpectator = isSpectator;
    }

    @Override
    protected String eventDataString() {
        return "party=" + party.toString() + ", mode=" + mode;
    }
}
