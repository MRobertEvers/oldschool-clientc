import { ChallengeType } from '@blert/common';

import { loadChallenge } from '@/actions/challenge';
import { withApiRoute } from '@/api/handler';

export const GET = withApiRoute(
  { route: '/api/v1/challenges/inferno/[id]' },
  async (_, { params }) => {
    const { id } = await params;
    const challenge = await loadChallenge(ChallengeType.INFERNO, id);
    if (challenge === null) {
      return new Response(null, { status: 404 });
    }
    return Response.json(challenge);
  },
);
