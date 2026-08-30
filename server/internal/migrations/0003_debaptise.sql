-- 0003 — trois jeux débaptisés, et les classements qui les suivent.
--
-- « PAC-MAN », « TETRIS » et « FLAPPY BIRD » sont des marques déposées — Bandai
-- Namco, Tetris Holding, Dong Nguyen — et le paquet ne pouvait pas être vendu
-- tant qu'elles y étaient. Les jeux s'appellent maintenant DÉDALE, APLOMB et
-- ENVOL. Les règles n'ont pas bougé d'une ligne : une mécanique ne s'approprie
-- pas, un nom si.
--
-- POURQUOI CE FICHIER EXISTE alors que `0001_initial.sql` porte déjà les
-- nouveaux noms : son `INSERT` se termine par `ON CONFLICT (id) DO NOTHING`.
-- Sur une base NEUVE il pose les bons créneaux et cette migration ne trouve
-- rien à renommer ; sur une base DÉJÀ SERVIE, il ne fait rien du tout, et les
-- créneaux resteraient « tetris-hard » pendant que le client demanderait
-- « aplomb-hard » — c'est-à-dire un classement mondial vide, sans message.
--
-- On renomme le CRÉNEAU, on ne le recrée pas : les parties et les scores
-- pointent vers `games.id`, qui ne bouge pas. Aucune ligne de classement n'est
-- perdue, elle change seulement d'étiquette.
UPDATE games SET slug = 'aplomb-hard', name = 'Aplomb' WHERE slug = 'tetris-hard';
UPDATE games SET slug = 'aplomb-easy', name = 'Aplomb' WHERE slug = 'tetris-easy';
UPDATE games SET slug = 'dedale',      name = 'Dédale' WHERE slug = 'pacman';
UPDATE games SET slug = 'envol-hard',  name = 'Envol'  WHERE slug = 'flappy-hard';
UPDATE games SET slug = 'envol-easy',  name = 'Envol'  WHERE slug = 'flappy-easy';
