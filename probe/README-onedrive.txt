Test OneDrive pour la PR "ChangeTime" (git-for-windows/git, ticket 3707)

Contenu
  pr\git.exe          Git avec les 3 commits de la PR
  rejected\git.exe    la variante ecartee : ChangeTime pour tous les fichiers
  cfattr.exe          lit les attributs et les heures d'un fichier
                      sans lire son contenu (rien n'est telecharge)
  test-onedrive.ps1   le script de test

Ce que fait le script
  - cree un dossier git-3707-test-<date> dans votre OneDrive, avec
    quelques petits depots Git de test (environ 300 Ko au total) ;
  - attend que OneDrive les ait envoyes ;
  - "libere de l'espace" sur ces seuls fichiers (attrib +U -P) ;
  - lance "git status" avec Git installe (s'il y en a un), la PR et la
    variante ecartee, avec et sans FSCache, et compte les fichiers que
    chaque Git fait retelecharger ;
  - recommence avec OneDrive ferme pendant environ une minute, puis le
    relance ;
  - ecrit tout dans result-<date>.txt, a cote du script.
  Il ne touche a rien d'autre.

Etapes
  1. Clic droit sur le ZIP > Proprietes > cocher "Debloquer" > OK,
     puis extraire le ZIP (hors de OneDrive, par exemple dans Telechargements).
  2. Dans le dossier extrait : clic droit dans le vide > "Ouvrir dans le
     terminal" (ou ouvrir PowerShell et faire cd vers ce dossier).
  3. Taper :
       powershell -ExecutionPolicy Bypass -File .\test-onedrive.ps1
  4. Attendre la fin (quelques minutes).
  5. Envoyer le fichier result-<date>.txt.
  6. Ensuite, supprimer le dossier git-3707-test-<date> dans OneDrive.
