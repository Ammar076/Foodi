-- Foodi seed — EU 14 major allergens + a starter synonym/derived-ingredient dictionary
-- Apply:  psql -h localhost -p 5433 -U foodi -d foodi -v ON_ERROR_STOP=1 -f migrations/002_seed_allergens.sql
-- Idempotent: ON CONFLICT DO NOTHING. The dictionary intentionally starts small
-- (high-risk derived terms) and should grow as gaps surface in real OFF data.

BEGIN;

-- The EU 14 (a superset of the US "Big 9"); off_tag matches Open Food Facts.
INSERT INTO allergens (off_tag, display_name) VALUES
  ('en:gluten',                        'Cereals containing gluten'),
  ('en:crustaceans',                   'Crustaceans'),
  ('en:eggs',                          'Eggs'),
  ('en:fish',                          'Fish'),
  ('en:peanuts',                       'Peanuts'),
  ('en:soybeans',                      'Soybeans'),
  ('en:milk',                          'Milk'),
  ('en:nuts',                          'Tree nuts'),
  ('en:celery',                        'Celery'),
  ('en:mustard',                       'Mustard'),
  ('en:sesame-seeds',                  'Sesame seeds'),
  ('en:sulphur-dioxide-and-sulphites', 'Sulphur dioxide and sulphites'),
  ('en:lupin',                         'Lupin'),
  ('en:molluscs',                      'Molluscs')
ON CONFLICT (off_tag) DO NOTHING;

-- Helper pattern: attach a list of normalized terms to one allergen by off_tag.

-- Gluten (oats deliberately omitted: gluten-free unless cross-contaminated, and
-- OFF tags them separately).
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('gluten'),('wheat'),('barley'),('rye'),('malt'),('semolina'),('durum'),
  ('spelt'),('farro'),('seitan'),('couscous'),('bulgur'),('triticale'),
  ('farina'),('kamut')
) AS t(term) WHERE a.off_tag = 'en:gluten'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Crustaceans
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('crustacean'),('crustaceans'),('shrimp'),('shrimps'),('prawn'),('prawns'),
  ('crab'),('lobster'),('crayfish'),('langoustine'),('krill'),('scampi')
) AS t(term) WHERE a.off_tag = 'en:crustaceans'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Eggs (token matching means 'egg' will not match 'eggplant')
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('egg'),('eggs'),('albumin'),('albumen'),('ovalbumin'),('ovomucoid'),
  ('lysozyme'),('mayonnaise'),('meringue')
) AS t(term) WHERE a.off_tag = 'en:eggs'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Fish
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('fish'),('anchovy'),('anchovies'),('cod'),('salmon'),('tuna'),('haddock'),
  ('sardine'),('sardines'),('mackerel'),('surimi'),('caviar'),('roe')
) AS t(term) WHERE a.off_tag = 'en:fish'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Peanuts
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('peanut'),('peanuts'),('groundnut'),('groundnuts'),('arachis')
) AS t(term) WHERE a.off_tag = 'en:peanuts'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Soybeans (bare 'lecithin' omitted: may be soy or sunflower)
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('soy'),('soya'),('soybean'),('soybeans'),('edamame'),('tofu'),('tempeh'),
  ('miso'),('tamari'),('natto')
) AS t(term) WHERE a.off_tag = 'en:soybeans'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Milk and derivatives (the terms naive substring matching would miss)
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('milk'),('buttermilk'),('cream'),('butter'),('cheese'),('whey'),('casein'),
  ('caseinate'),('caseinates'),('lactose'),('ghee'),('curd'),('curds'),
  ('yogurt'),('yoghurt'),('custard'),('kefir')
) AS t(term) WHERE a.off_tag = 'en:milk'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Tree nuts (specific nut names only; bare 'nut' omitted to avoid false friends)
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('almond'),('almonds'),('hazelnut'),('hazelnuts'),('walnut'),('walnuts'),
  ('cashew'),('cashews'),('pecan'),('pecans'),('pistachio'),('pistachios'),
  ('macadamia'),('praline'),('marzipan'),('nougat'),('gianduja')
) AS t(term) WHERE a.off_tag = 'en:nuts'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Celery
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('celery'),('celeriac')
) AS t(term) WHERE a.off_tag = 'en:celery'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Mustard
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('mustard')
) AS t(term) WHERE a.off_tag = 'en:mustard'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Sesame
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('sesame'),('tahini'),('gingelly'),('benne'),('sesamol')
) AS t(term) WHERE a.off_tag = 'en:sesame-seeds'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Sulphites (incl. the E220–E228 additive codes)
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('sulphite'),('sulphites'),('sulfite'),('sulfites'),('metabisulphite'),
  ('metabisulfite'),('bisulphite'),('bisulfite'),
  ('e220'),('e221'),('e222'),('e223'),('e224'),('e226'),('e227'),('e228')
) AS t(term) WHERE a.off_tag = 'en:sulphur-dioxide-and-sulphites'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Lupin
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('lupin'),('lupine'),('lupins')
) AS t(term) WHERE a.off_tag = 'en:lupin'
ON CONFLICT (allergen_id, term) DO NOTHING;

-- Molluscs
INSERT INTO allergen_synonyms (allergen_id, term)
SELECT a.id, t.term FROM allergens a CROSS JOIN (VALUES
  ('mollusc'),('molluscs'),('mollusk'),('mollusks'),('mussel'),('mussels'),
  ('oyster'),('oysters'),('clam'),('clams'),('squid'),('octopus'),('scallop'),
  ('scallops'),('snail'),('snails'),('cuttlefish'),('abalone'),('whelk'),
  ('periwinkle')
) AS t(term) WHERE a.off_tag = 'en:molluscs'
ON CONFLICT (allergen_id, term) DO NOTHING;

COMMIT;
