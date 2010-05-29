t = db.jstests_subset;
t.drop()

doTest = function() {
    t.save({a: [1]});
    t.save({a: [1,1]});
    t.save({a: [1,2]});
    t.save({a: [1,2,2]});
    t.save({a: [1,2,3]});
    t.save({a: [1,2,4]});
    t.save({a: [3,1,2]});
    t.save({a: [-1, 1, 2]});
    t.save({a: {b: [1,2]}});
    t.save({a: {b: [1,2,3]}});

    assert.eq(2, t.find({a: {$subset: [1]}}).count());
    assert.eq(6, t.find({a: {$subset: [1,2,3]}}).count());
    assert.eq(0, t.find({a: {$subset: [2]}}).count());
    assert.eq(0, t.find({a: {$subset: []}}).count());
    assert.eq(1, t.find({"a.b": {$subset: [1,2]}}).count());
    assert.eq(0, t.find({"a.b": {$subset: [1]}}).count());
};

doTest();
