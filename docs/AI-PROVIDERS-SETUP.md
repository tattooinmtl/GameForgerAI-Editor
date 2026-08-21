# Configuration des providers IA

La configuration est dans `Game/AI/Providers.json`.
Les clés locales sont dans `Game/AI/Providers.local.json`, qui est ignoré par Git.

NVIDIA est le provider principal. Agnes-AI est le provider de secours.

## Clés API locales au projet

Les clés ne doivent pas être écrites dans `Providers.json`.
Pour ne pas utiliser les variables d’environnement du PC, ouvrez `Game/AI/Providers.local.json` et remplacez les deux valeurs `COLLER_ICI...` par vos nouvelles clés.

Ce fichier est exclu de Git par `.gitignore` et ne doit jamais être partagé.

Le client IA de l’éditeur lira le nom indiqué par `localSecretsFile` dans `Providers.json`.

### Alternative : variables d’environnement

Si le fichier local n’est pas disponible, le client pourra utiliser les variables d’environnement suivantes.

PowerShell, session courante :

```powershell
$env:NVIDIA_API_KEY = "votre-cle-nvidia"
$env:AGNES_AI_API_KEY = "votre-cle-agnes-ai"
```

Pour les enregistrer pour l’utilisateur Windows :

```powershell
[Environment]::SetEnvironmentVariable("NVIDIA_API_KEY", "votre-cle-nvidia", "User")
[Environment]::SetEnvironmentVariable("AGNES_AI_API_KEY", "votre-cle-agnes-ai", "User")
```

Redémarrer Visual Studio après une modification persistante des variables.

## Providers configurés

### NVIDIA principal

- Endpoint : `https://integrate.api.nvidia.com/v1/chat/completions`
- Variable de clé : `NVIDIA_API_KEY`
- Modèle par défaut : `nvidia/nemotron-3-nano-30b-a3b`

### Agnes-AI secours

- Endpoint : `https://apihub.agnes-ai.com/v1/chat/completions`
- Variable de clé : `AGNES_AI_API_KEY`
- Modèle : `agnes-2.0-flash`

Les deux endpoints utilisent le protocole compatible OpenAI. Le client IA de l’éditeur devra envoyer les commandes d’outils avec un schéma JSON validé et refuser toute réponse qui ne passe pas le validateur local.
