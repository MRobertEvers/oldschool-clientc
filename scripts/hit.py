def hit_chance(atk, defense):
    if atk > defense:
        return 1 - (defense + 2) / (2 * (atk + 1))
    else:
        return atk / (2 * (defense + 1))


def generate_values(defense, atk_min=0, atk_max=20):
    print(f"Defense = {defense}")
    print("Attack\tHit Chance")
    print("-------------------")
    for atk in range(atk_min, atk_max + 1):
        hc = hit_chance(atk, defense)
        # clamp between 0 and 1
        hc = max(0.0, min(1.0, hc))
        print(f"{atk}\t{hc:.4f}")


if __name__ == "__main__":
    defense = 1000   # hold this constant
    generate_values(defense, 0, 10000)
