import {
  ChallengeMode,
  challengeName,
  ChallengeStatus,
  ChallengeType,
  SplitType,
  Stage,
} from '@blert/common';

import { ChallengeQuery, SortableFields } from '@/actions/challenge';
import { InvalidQueryError } from '@/actions/errors';
import {
  Comparator,
  Condition,
  Operator,
  comparatorMatches,
  parseQuery,
  SortQuery,
} from '@/actions/query';
import {
  comparatorParam,
  dateComparatorParam,
  enumComparatorParam,
  enumListParam,
  expectSingle,
  integerComparatorParam,
  integerComparatorValue,
  isValidEnumValue,
  spawnQueryValue,
} from '@/api/query';
import { handicapFromToken } from '@/utils/colosseum';
import { NextSearchParams, requestParams } from '@/utils/url';

type NamespacedParamHandler = {
  /** Parses the key segment to an id, or null if invalid. Defaults to numeric. */
  parseKey?: (key: string) => number | null;
  validateKey?: (id: number) => boolean;
  apply: (
    query: ChallengeQuery,
    id: number,
    comparator: Comparator<number>,
  ) => void;
};

const namespacedParams: Record<string, NamespacedParamHandler> = {
  split: {
    validateKey: (id) => isValidEnumValue(SplitType, id),
    apply: (query, id, comparator) => {
      (query.splits ??= new Map()).set(id, comparator);
    },
  },
  'tob.bloatDown': {
    validateKey: (id) => id >= 1,
    apply: (query, id, comparator) => {
      ((query.tob ??= {}).bloatDowns ??= new Map()).set(id, comparator);
    },
  },
  'colo.handicap': {
    parseKey: handicapFromToken,
    apply: (query, id, comparator) => {
      if (!valuesInRange(comparator, 0, 3)) {
        throw new InvalidQueryError(
          `Invalid handicap level: ${comparator[1].toString()}`,
        );
      }
      ((query.colosseum ??= {}).levels ??= new Map()).set(id, comparator);
    },
  },
};

function valuesInRange(
  comparator: Comparator<number>,
  min: number,
  max: number,
): boolean {
  const inRange = (value: number) => value >= min && value <= max;
  switch (comparator[0]) {
    case 'in':
    case 'nin':
    case 'range':
      return comparator[1].every(inRange);
    default:
      return inRange(comparator[1]);
  }
}

/**
 * Parses URL parameters for a query that filters challenges.
 * @throws InvalidQueryError If a parameter is malformed.
 */
export function parseChallengeQueryParams(
  searchParams: URLSearchParams,
): ChallengeQuery {
  return parseChallengeQuery(requestParams(searchParams));
}

/**
 * Parses URL parameters for a query that filters challenges.
 * @throws InvalidQueryError If a parameter is malformed.
 */
export function parseChallengeQuery(
  searchParams: NextSearchParams,
): ChallengeQuery {
  const party = expectSingle(searchParams, 'party')?.split(',') ?? undefined;
  const mode = enumListParam(ChallengeMode, searchParams, 'mode');

  const query: ChallengeQuery = {
    mode,
    party,
    splits: new Map(),
    customConditions: [],
  };

  const before = expectSingle(searchParams, 'before');
  const after = expectSingle(searchParams, 'after');
  if (before !== undefined && after !== undefined) {
    throw new InvalidQueryError('Cannot page with both before and after');
  }

  const reverseSorts = before !== undefined;

  const sort = expectSingle(searchParams, 'sort');
  let sortFields: SortQuery<SortableFields>[] = [];
  if (sort !== undefined) {
    query.sort = [];

    const fields = sort.split(',');
    if (fields.length === 0 || fields.length > 2) {
      throw new InvalidQueryError('sort: Expected one or two fields');
    }

    for (const sort of fields) {
      const sortOp = sort[0];
      if (sortOp !== '-' && sortOp !== '+') {
        throw new InvalidQueryError(`sort: Invalid direction ${sort}`);
      }

      const sortField = sort.slice(1) as SortableFields;
      const sortDirection = reverseSorts
        ? sortOp === '+'
          ? '-'
          : '+'
        : sortOp;
      const options = reverseSorts ? 'nf' : 'nl';
      query.sort.push(
        `${sortDirection}${sortField}#${options}` as SortQuery<SortableFields>,
      );
    }

    sortFields = query.sort;
  }

  if (before !== undefined) {
    const condition = paginationCondition(sortFields, before.split(','), true);
    if (condition === null) {
      throw new InvalidQueryError(`before: Invalid cursor ${before}`);
    }
    query.customConditions!.push(condition);
  } else if (after !== undefined) {
    const condition = paginationCondition(sortFields, after.split(','), false);
    if (condition === null) {
      throw new InvalidQueryError(`after: Invalid cursor ${after}`);
    }
    query.customConditions!.push(condition);
  }

  for (const [key, value] of Object.entries(searchParams)) {
    const colonIndex = key.indexOf(':');
    if (colonIndex === -1) {
      continue;
    }

    const handler = namespacedParams[key.slice(0, colonIndex)];
    if (handler === undefined) {
      continue;
    }

    if (value === undefined || Array.isArray(value)) {
      throw new InvalidQueryError(`${key}: Expected a single value`);
    }

    const rawKey = key.slice(colonIndex + 1);
    if (rawKey === '') {
      throw new InvalidQueryError(`${key}: Missing key`);
    }

    let id: number | null;
    if (handler.parseKey !== undefined) {
      id = handler.parseKey(rawKey);
    } else {
      const parsed = Number(rawKey);
      id = Number.isFinite(parsed) ? parsed : null;
    }
    if (id === null) {
      throw new InvalidQueryError(`${key}: Invalid key ${rawKey}`);
    }

    if (handler.validateKey !== undefined && !handler.validateKey(id)) {
      throw new InvalidQueryError(`${key}: Invalid key ${rawKey}`);
    }

    handler.apply(query, id, integerComparatorValue(value));
  }

  query.type = enumComparatorParam(ChallengeType, searchParams, 'type');
  query.scale = integerComparatorParam(searchParams, 'scale', 1, 8);
  query.status = enumComparatorParam(ChallengeStatus, searchParams, 'status');
  query.startTime = dateComparatorParam(searchParams, 'startTime');
  query.challengeTicks = integerComparatorParam(
    searchParams,
    'challengeTicks',
    0,
  );
  query.stage = enumComparatorParam(Stage, searchParams, 'stage');

  const spawn = searchParams.spawn;
  if (spawn !== undefined) {
    let type = query.type?.[0] === '==' ? query.type[1] : null;
    query.spawns = [];
    for (const value of Array.isArray(spawn) ? spawn : [spawn]) {
      const [parsed, challenge] = spawnQueryValue(value, type);
      query.spawns.push(parsed);
      type = challenge;
    }
    if (type !== null) {
      if (query.type !== undefined && !comparatorMatches(query.type, type)) {
        throw new InvalidQueryError(
          `spawn: npc/coords for ${challengeName(type)} contradict "type" param`,
        );
      }
      // Narrow the type since the spawn query can only match it.
      query.type = ['==', type];
    }
  }

  const tobScalarParams = [
    'bloatDownCount',
    'nylocasPreCapStalls',
    'nylocasPostCapStalls',
    'xarpusHealing',
    'verzikRedsCount',
  ] as const;
  for (const field of tobScalarParams) {
    const value = integerComparatorParam(searchParams, `tob.${field}`, 0, 9001);
    if (value !== undefined) {
      (query.tob ??= {})[field] = value;
    }
  }

  const mokhaiotlScalarParams = [
    'maxCompletedDelve',
    'avgDeepDelveTicks',
  ] as const;
  for (const field of mokhaiotlScalarParams) {
    const value = integerComparatorParam(searchParams, `mok.${field}`, 0, 1000);
    if (value !== undefined) {
      (query.mokhaiotl ??= {})[field] = value;
    }
  }

  const handicap = comparatorParam(searchParams, 'colo.handicap', (token) => {
    const h = handicapFromToken(token);
    if (h === null) {
      throw new InvalidQueryError(`Invalid handicap: ${token}`);
    }
    return h;
  });
  if (handicap !== undefined) {
    const op = handicap[0];
    if (op !== '==' && op !== '!=' && op !== 'in' && op !== 'nin') {
      throw new InvalidQueryError(`Invalid handicap operator: ${op}`);
    }
    (query.colosseum ??= {}).has = handicap;
  }

  const customQuery = expectSingle(searchParams, 'q');
  if (customQuery !== undefined) {
    let customConditions: Condition | null;
    try {
      customConditions = parseQuery(atob(customQuery));
    } catch {
      customConditions = null;
    }
    if (customConditions === null) {
      throw new InvalidQueryError('q: Invalid query');
    }
    query.customConditions!.push(customConditions);
  }

  return query;
}

function paginationCondition(
  sorts: SortQuery<SortableFields>[],
  values: string[],
  reverse: boolean,
): Condition | null {
  if (sorts.length !== values.length) {
    return null;
  }

  const [primarySort, secondarySort] = sorts;
  const [primaryValue, secondaryValue] = values;

  let secondaryCondition: Condition | null = null;
  if (secondarySort !== undefined) {
    const secondaryField = secondarySort
      .slice(1)
      .split('#')[0] as SortableFields;
    const op = secondarySort.startsWith('+') ? '>' : '<';
    const val = Number.parseInt(secondaryValue);
    if (Number.isNaN(val)) {
      return null;
    }
    secondaryCondition = [secondaryField, op, val];
  }

  const operator: Operator = primarySort.startsWith('+') ? '>' : '<';
  const sortField = primarySort.slice(1).split('#')[0] as SortableFields;

  const value = primaryValue === 'null' ? null : Number.parseInt(primaryValue);
  if (Number.isNaN(value)) {
    return null;
  }

  let cond: Condition | null = null;
  if (value !== null) {
    cond = [sortField, operator, value];
    if (!reverse) {
      cond = [cond, '||', [sortField, 'is', null]];
    }
  }

  if (secondaryCondition !== null) {
    let secondary: Condition;
    if (reverse && value === null) {
      // When paging backwards, null values will be first, followed by
      // non-null values.
      const isNotNull: Condition = [sortField, 'isnot', null];
      const isNullBefore: Condition = [
        [sortField, 'is', null],
        '&&',
        secondaryCondition,
      ];
      secondary = [isNotNull, '||', isNullBefore];
    } else {
      secondary = [[sortField, '==', value], '&&', secondaryCondition];
    }

    if (cond === null) {
      cond = secondary;
    } else {
      cond = [cond, '||', secondary];
    }
  }

  return cond;
}
