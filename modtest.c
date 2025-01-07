#include <assert.h>
#include "modsystem.h"

int main() {
    ModList* mods = (ModList*)malloc(sizeof(ModList));
    mods->count = 0;

    Mod* lfo1 = createMod(0);
    Mod* lfo2 = createMod(1);

    addDestination(lfo1, lfo2); // lfo1 modulates lfo2

    addMod(mods, lfo1);
    addMod(mods, lfo2);

    // Check that the mods are added correctly
    assert(mods->count == 2);
    assert(mods->mods[0] == lfo1);
    assert(mods->mods[1] == lfo2);

    // Check that the destination is set correctly
    assert(lfo1->destinations == lfo2);
    assert(lfo2->destinations == NULL);

    ModList* sortedMods = topologicalSort(mods);

    // Check that the topological sort produces the correct order
    assert(sortedMods->count == 2);
    assert(sortedMods->mods[0] == lfo1);
    assert(sortedMods->mods[1] == lfo2);

    // Calculate outputs in the correct order
    for (int i = 0; i < sortedMods->count; i++) {
        // Call the generate function for each modulator
        // sortedMods->mods[i]->base->generate(sortedMods->mods[i]);
    }

    freeModList(sortedMods);
    freeModList(mods);

    printf("All tests passed.\n");

    return 0;
}