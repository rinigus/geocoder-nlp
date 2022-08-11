select c.place_id,c.name,c.parent_place_id,c.linked_place_id,c.osm_id,c.osm_type,c.indexed_status 
from placex c 
join placex r on r.place_id=c.parent_place_id 
where r.parent_place_id=c.place_id;
