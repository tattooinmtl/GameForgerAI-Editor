# Plan d’implémentation — GameForgerAI Editor

> **Note aux agents et développeurs :** Pour le plan de correction prioritaire, les audits comparatifs avec Unity 3D et le suivi de session en cours, consultez impérativement :
> - [`AGENTS.md`](file:///C:/GameForgerAI-Editor/AGENTS.md) — Guide d'exploitation pour agents IA.
> - [`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md) — Plan directeur actif (correctifs, parité Unity 3D, et objectifs IA Forge R-28/R-29 issus de ce fichier). Remplace `planFix_AGY.v1.2026-08-18.md` (archivé).
> - [`planFix_AGY_CHANGELOG.md`](file:///C:/GameForgerAI-Editor/docs/planFix_AGY_CHANGELOG.md) — Journal des modifications et avancement des tickets.
> - [`Compared.md`](file:///C:/GameForgerAI-Editor/docs/Compared.md) — Matrice comparative détaillée avec Unity 3D.

## Objectif

Transformer le prototype actuel en éditeur de jeux 3D fonctionnel sous Windows, basé sur OpenGL, capable d’importer et d’afficher des modèles Blender aux formats `.glb`, `.gltf` et `.fbx`.

L’éditeur intégrera également une couche IA de type Copilot, accessible dans un panneau dédié à droite. Cette couche pourra assister la création de scènes, de modèles et de jeux complets, mais uniquement via des outils typés, validés et contrôlés par l’éditeur.

Le développement sera progressif : chaque étape devra compiler et démarrer avant de passer à la suivante.

## Contraintes techniques

- C++20.
- CMake avec générateur Ninja Multi-Config.
- MSVC x64.
- OpenGL 4.6 avec GLFW et GLAD.
- Dear ImGui pour l’interface de l’éditeur.
- GLM pour les mathématiques 3D.
- Compatibilité Blender prioritairement via glTF 2.0 (`.glb` et `.gltf`).
- Import FBX via une bibliothèque dédiée, avec conversion vers le format de scène interne.
- Ne jamais modifier directement la mémoire de scène depuis une commande IA : utiliser des commandes typées, validées et annulables.
- L’IA ne possède pas d’accès direct illimité au système de fichiers, au processus ou à la mémoire du moteur.
- Toute action IA doit pouvoir être prévisualisée, validée, annulée et journalisée.

## Providers IA configurés

- NVIDIA est le provider principal via `https://integrate.api.nvidia.com/v1/chat/completions`.
- Agnes-AI est le provider de secours via `https://apihub.agnes-ai.com/v1/chat/completions` avec le modèle `agnes-2.0-flash`.
- Les clés sont fournies uniquement par `NVIDIA_API_KEY` et `AGNES_AI_API_KEY`.
- La configuration est stockée dans `Game/AI/Providers.json` et référencée par `Game/Project.json`.
- Le modèle NVIDIA reste configurable dans ce fichier sans modifier le code du moteur.

## Architecture de la couche IA

La couche IA sera séparée du renderer et du cœur de la scène :

```text
Panneau AI Forge
		↓
Conversation et contexte du projet
		↓
Plan d’actions structuré
		↓
Validateur de commandes
		↓
Bus d’outils de l’éditeur
		↓
Scène, assets, scripts et configuration du jeu
```

### Panneau AI Forge

Le panneau sera placé à droite de l’éditeur et comprendra :

- historique de conversation ;
- zone de prompt multiline ;
- bouton d’envoi et annulation ;
- sélection du contexte : scène, entité, asset ou projet ;
- affichage du plan généré ;
- aperçu des changements ;
- boutons `Prévisualiser`, `Appliquer`, `Annuler` ;
- liste des erreurs et avertissements ;
- indicateur d’état de l’outil IA.

### Outils accessibles à l’IA

Les outils seront des commandes explicites et validées :

- créer, supprimer et modifier une entité ;
- modifier un transform ;
- créer une caméra ou une lumière ;
- importer un modèle GLB, GLTF ou FBX ;
- créer un matériau et assigner des textures ;
- créer une scène ou un niveau ;
- générer une hiérarchie d’objets ;
- créer des scripts et composants ;
- configurer les entrées et les règles de gameplay ;
- créer les fichiers de projet nécessaires ;
- lancer une validation ou un aperçu du jeu.

Chaque outil aura :

- un schéma d’entrée ;
- des permissions ;
- une validation ;
- une description lisible par l’utilisateur ;
- une opération inverse pour undo/redo ;
- un résultat structuré et journalisé.

### Création assistée de modèles 3D

L’IA pourra créer ou préparer des modèles 3D de plusieurs façons :

- générer une primitive ou une composition procédurale ;
- créer une scène de référence à partir d’un prompt ;
- importer un modèle généré par un outil externe ;
- convertir et placer un fichier GLB/GLTF/FBX ;
- générer les matériaux et les assignations de textures ;
- demander à l’utilisateur de confirmer les fichiers ou services externes nécessaires.

Les ressources générées seront traitées comme des assets ordinaires : import, cache, prévisualisation, validation et sauvegarde.

### Création guidée d’un jeu complet

La création d’un jeu complet sera progressive et pilotée par un plan de projet :

1. définir le genre, la plateforme et les objectifs ;
2. créer la structure du projet ;
3. créer les scènes et les entités ;
4. importer ou générer les assets ;
5. créer les scripts et règles de gameplay ;
6. configurer les contrôles, UI, audio et paramètres ;
7. exécuter les validations ;
8. lancer un aperçu jouable ;
9. présenter les erreurs restantes et les corrections proposées.

L’IA ne fera pas une modification massive opaque : chaque étape produira un plan consultable et des commandes annulables.

## Décisions de format

### glTF / GLB

Format principal recommandé pour Blender :

- maillages 3D ;
- matériaux PBR ;
- textures ;
- armatures ;
- animations ;
- hiérarchie de nœuds.

Le fichier `.glb` sera le premier format supporté complètement.

### FBX

Le support FBX sera ajouté ensuite par conversion vers les structures internes du moteur. Le format FBX étant propriétaire, le plan prévoit une bibliothèque d’import compatible plutôt que l’intégration directe du SDK Autodesk.

Le niveau de support sera documenté : géométrie, matériaux, armatures et animations seront traités progressivement.

## Phase 1 — Stabiliser la base de compilation

- Vérifier les cibles `GameForgerEngine`, `GameForgerEditor` et `GameForgerRuntime`.
- Conserver le preset `windows-x64` et le générateur Ninja Multi-Config.
- Ajouter des vérifications CMake pour les dépendances nécessaires.
- Corriger les erreurs d’assertion ImGui et les erreurs de démarrage.
- Ajouter un script clair pour configurer, compiler et lancer l’éditeur.
- Vérifier le démarrage en Debug et en Release.

**Résultat attendu :** l’éditeur démarre de manière reproductible sans fenêtre blanche ni `abort()`.

## Phase 2 — Architecture OpenGL de rendu

Créer dans le moteur :

- `RenderDevice` pour l’initialisation OpenGL ;
- `Shader` et compilation GLSL ;
- `VertexBuffer`, `IndexBuffer` et `VertexArray` ;
- `Texture2D` ;
- `Mesh` ;
- `Material` ;
- `Renderer` ;
- gestion des erreurs OpenGL ;
- vérification de la version OpenGL disponible.

Ajouter un premier shader simple permettant d’afficher un triangle, puis un maillage avec caméra et profondeur.

**Résultat attendu :** le viewport affiche une scène OpenGL réelle au lieu d’un simple panneau ImGui.

## Phase 3 — Scène 3D et transformations

Ajouter les structures suivantes :

- `Entity` ;
- `Transform` ;
- `Scene` ;
- `Camera` ;
- `Light` ;
- hiérarchie parent-enfant ;
- matrices modèle, vue et projection ;
- sérialisation JSON de la scène.

Ajouter une caméra orbitale dans le viewport :

- rotation avec la souris ;
- zoom ;
- déplacement latéral ;
- sélection d’objet ;
- grille et axes de référence.

**Résultat attendu :** création et manipulation d’objets 3D dans une scène persistante.

## Phase 4 — Import glTF / GLB

Intégrer un importeur glTF léger et adapté au runtime. L’importeur devra convertir les données vers les ressources internes :

- nœuds ;
- maillages ;
- indices et sommets ;
- normales et tangentes ;
- UV ;
- matériaux PBR ;
- textures ;
- armatures ;
- animations.

Créer un cache de ressources pour éviter de recharger plusieurs fois le même modèle.

Ajouter au navigateur d’assets :

- détection des fichiers `.glb` et `.gltf` ;
- aperçu ;
- import dans la scène par glisser-déposer ;
- messages d’erreur lisibles.

**Résultat attendu :** un modèle exporté depuis Blender en `.glb` apparaît correctement dans le viewport.

## Phase 5 — Import FBX

Ajouter un importeur FBX séparé, sans mélanger son code avec l’importeur glTF.

Pipeline prévu :

1. charger le fichier FBX ;
2. convertir les nœuds vers la hiérarchie interne ;
3. convertir les maillages ;
4. convertir les matériaux et textures disponibles ;
5. convertir les armatures ;
6. convertir les animations ;
7. enregistrer le résultat dans le cache de ressources.

Tester les fichiers FBX exportés depuis Blender avec :

- un modèle statique ;
- un modèle avec textures ;
- un personnage avec armature ;
- une animation.

**Résultat attendu :** les modèles FBX courants de Blender peuvent être importés avec un rapport d’avertissements si certaines fonctions ne sont pas supportées.

## Phase 6 — Matériaux, textures et éclairage

- Shader PBR de base.
- Albedo/base color.
- Normal map.
- Metallic/roughness.
- Occlusion.
- Émission.
- Chargement des textures référencées.
- Recherche de textures relative au fichier importé.
- Lumière directionnelle et lumières ponctuelles.
- Mode prévisualisation matériau.

**Résultat attendu :** les modèles Blender importés ont un rendu visuel cohérent dans l’éditeur.

## Phase 7 — Animation et armatures

- Structures de squelette et de pose.
- Upload des matrices de joints vers le GPU.
- Skinning GPU.
- Lecture d’animations.
- Timeline dans l’éditeur.
- Lecture, pause et scrubbing.
- Sélection d’un os.
- Affichage debug du squelette.
- Préparation de la fusion d’animations et de l’IK.

**Résultat attendu :** un personnage Blender avec animation peut être affiché et lu dans le viewport.

## Phase 8 — Interface éditeur

Remplacer les panneaux prototypes par des panneaux fonctionnels :

- scène ;
- inspector ;
- navigateur d’assets ;
- viewport ;
- matériaux ;
- animation ;
- console de messages ;
- paramètres du projet.
- panneau AI Forge dockable à droite, avec contexte, prompts, plans et aperçu des changements.

Ajouter les opérations essentielles :

- nouveau projet ;
- ouvrir une scène ;
- sauvegarder ;
- importer un modèle ;
- supprimer une entité ;
- dupliquer une entité ;
- annuler/rétablir.

## Phase 9 — Runtime

- Charger `Game/Project.json`.
- Charger la scène de démarrage.
- Charger les ressources importées.
- Afficher la scène avec le même moteur de rendu que l’éditeur.
- Séparer clairement les fonctionnalités Editor et Runtime.
- Préparer le chargement des scripts Lua sans bloquer le rendu.

**Résultat attendu :** le runtime ouvre réellement le projet et affiche la scène de démarrage.

## Phase 10 — Couche IA, outils et génération de projets

- Ajouter le contrat C++ `AIEditorCommand` pour les entités, scripts et propriétés exposées.
- Ajouter `AICommandValidator` et `AICommandBus` comme point d’entrée commun du panneau AI et de VS Code.
- Définir le modèle de conversation et le contexte de projet.
- Définir les commandes d’édition typées et versionnées.
- Implémenter le validateur de schémas et de permissions.
- Implémenter le bus d’outils de l’éditeur.
- Ajouter le mode planification avec aperçu avant application.
- Ajouter undo/redo transactionnel.
- Ajouter le journal des actions, résultats et erreurs.
- Ajouter les outils de création de scènes, assets, scripts et projets.
- Ajouter les workflows guidés de création de jeux complets.
- Prévoir un adaptateur pour un service IA local ou distant sans coupler le moteur à un fournisseur.
- Ne jamais laisser une réponse IA modifier directement les objets de la scène.

## Tests et validation

À chaque phase :

- vérifier la structure des fichiers avant compilation ;
- compiler la cible minimale concernée ;
- lancer l’exécutable correspondant ;
- tester un cas nominal ;
- tester un fichier invalide ;
- vérifier l’absence d’assertions Debug ;
- vérifier les chemins relatifs depuis la racine du projet.

Tests de contenu Blender prévus :

- cube `.glb` ;
- personnage `.glb` avec texture ;
- personnage `.glb` avec animation ;
- modèle `.fbx` statique ;
- modèle `.fbx` riggé ;
- textures PNG/JPG référencées.

## Ordre de livraison recommandé

1. Stabilisation CMake et démarrage.
2. Renderer OpenGL minimal.
3. Caméra et scène 3D.
4. Import `.glb`.
5. Matériaux et textures.
6. Import `.fbx`.
7. Armatures et animations.
8. Interface éditeur fonctionnelle.
9. Runtime de scène.
10. IA, commandes validées et undo/redo.

## Critère de réussite final

Depuis Blender, l’utilisateur peut exporter un modèle `.glb` ou `.fbx`, l’importer dans GameForgerAI Editor, le voir dans une scène OpenGL, modifier sa transformation, sauvegarder la scène, puis l’ouvrir dans `GameForgerRuntime`.
